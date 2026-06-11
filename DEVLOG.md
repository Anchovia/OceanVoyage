# DEVLOG — OceanVoyage

Vulkan 공부 겸 엔진 개발 기록.

---

## 구현 기록

### 2026-06-12 — 반사 비용 정책: REFL 모드 4종 (ROADMAP Phase 4-5)

- 설정에 `REFL` 행 추가: SKY(0)/SSR(1)/PLANAR(2)/FULL(3, 기본 — 기존과 동일). **빌드·검증 완료(기본 모드 화면 동일, 모드 전환 즉시 반영).**
- `ocean.frag`가 `temporalParams.y`로 모드 수신 — SSR 28스텝 레이마치는 1/3에서만(uniform branch), 플래너 기여는 2/3에서만. 플래너 패스는 모드 0/1에서 미러 지오메트리 draw 스킵(패스/클리어는 유지 — 클리어 색=하늘이라 정확하고 레이아웃 사이클 보존, 향후 항구/섬 draw가 실질 절감 대상).
- 의도적 보류: 플래너 반해상도 — 시각 트레이드오프라 묵시 적용 안 함(사용자 결정 대기, 상용 관행이긴 함).

### 2026-06-12 — 셰이더 상수 단일 출처화: shared_constants.h (ROADMAP Phase 4-6)

- C++/GLSL 겸용 순수 `#define` 헤더 `shaders/shared_constants.h` 신설 — `OCEAN_FFT_N`(512)/`CASCADE_L`(셰이더 6종 중복이던 최다)/`SEA_LEVEL`/`WAKE_N·WORLD_SIZE`/`SHADOW_MAP_SIZE`의 단일 출처. **빌드·검증 완료(화면 동일).**
- 배선: glslc `-I` + `GL_GOOGLE_include_directive`, 헤더가 셰이더 커스텀 커맨드 DEPENDS에 포함 → 헤더 수정 시 전 셰이더 재컴파일. C++ 멤버명 유지(호출부 무수정). ship.frag 하드코딩 `1/2048` 제거(ENGINE_TODO P2 그림자 항목 일부 해소).
- push 기반이라 중복 없던 `ocean_fft.comp`/`ocean_wake.comp`는 제외.

### 2026-06-12 — 부력 리드백 축소: GPU 5점 샘플 (ROADMAP Phase 4-4)

- 전체 displacement 맵 host 복사(512²×3 RGBA16F ≈ 6MiB/프레임)를 **GPU 컴퓨트 1스레드 5점 샘플 + 20B 리드백**으로 교체. **빌드·검증 완료(부력 동작 동일, validation 무경고).**
- `ocean_buoyancy.comp` 신규: 역변위 고정점 솔브 3회 + 중심/±x/±y 높이 5개 → host-visible SSBO(frame-in-flight당 20B). CPU 수동 bilinear와 하드웨어 `textureLod`(linear·repeat 공유 샘플러)는 동일 수식. 2프레임 지연 리드백 타이밍 동일.
- 제거: `vkCmdCopyImageToBuffer` 6MiB 복사, displacement `TRANSFER_SRC` usage, CPU half-float 디코드/bilinear/솔브 헬퍼 4개(~55줄). FFT 디스크립터 풀 확장(8→10 sets).
- 미세 차이(명시): 샘플 위치가 기록 시점(2프레임 전) 배 위치 — 최고속 기준 ~0.3m, 파장 대비 무시 가능.
- 부작용: `CASCADE_L` must-match 리터럴 중복이 셰이더 5곳으로 증가 → Phase 4-6(상수 단일 출처화)의 우선순위 상승.

### 2026-06-12 — SMAA 색공간 정리: 톤매핑 후 SMAA (ROADMAP Phase 4-3)

- SMAA 모드의 패스 순서를 표준으로 교체: `scene(HDR) → SMAA → 톤매핑` → **`scene(HDR) → 톤매핑+그레이딩(LDR R8G8B8A8_SRGB 타겟) → SMAA 3패스(순수 resolve) → 스왑체인`**. **빌드·검증 완료(색감 동일, 고대비 엣지 개선, validation 무경고).**
- HDR 픽셀을 직접 섞던 neighborhood의 비표준 블렌딩 제거. `smaa_edge`는 자체 ACES 변환 삭제(감마 luma만), `smaa_neighborhood`는 중복 톤매핑/그레이딩/비네팅 삭제 → **그레이드 상수가 `post.frag` 단일 출처**가 됨.
- 구현: LDR 타겟(frame-in-flight당 1장, 스왑체인과 동일 sRGB 인코딩) + `post.frag` 재사용 LDR 출력 파이프라인(렌더패스만 다름). OFF/FXAA/TAA 경로 무변경.

### 2026-06-12 — TAA 1차 (HDR resolve 패스) — 옵션으로 동결 (ROADMAP Phase 4-2a)

- aaMode 3 = TAA 추가(기본값은 SMAA 유지). scene(HDR) → TAA resolve → post(톤매핑) 구조. **빌드·동작 검증 완료(윤슬 shimmer 안정화 확인, validation 무경고).**
- 구현: `taa.frag` 신규 — 깊이 기반 재투영(SSR `prevViewProj` 재사용), 3×3 neighborhood clamp, 히스토리 0.9 블렌드. HDR 이미지 2장 핑퐁(frame-in-flight 인덱스 교대), 첫 프레임/모드 전환/리사이즈 시 히스토리 무효화. depth에 `SAMPLED` usage 추가 + 패스 전 배리어. `post.frag` FXAA 게이트 `!=0`→`==1` 수정(TAA 위 FXAA 중첩 방지).
- **알려진 한계(의도): 이동 중 흐림.** 체이스 카메라가 항상 미세 이동 → bilinear 히스토리 재샘플 블러 누적. 표준 해법 3종(Halton 지터 / Catmull-Rom 히스토리 샘플링 / 모션 적응 블렌드+샤프닝)은 **2차 슬라이스로 보류**(사용자 결정 2026-06-12). 보류 해제 시 이 3종 구현 후 "정지 화면이 SMAA보다 선명한가"로 재평가.
- 기본 AA는 SMAA 유지 — TAA는 opt-in 옵션으로만 노출.

### 2026-06-11 — 두 번째 항구 + 첫 교역 루프 성립 (ROADMAP Phase 3c-3, **Phase 3 완료**)

- `LIVERPOOL` 추가(BRISTOL 정동 600m). 가격 차별화로 양방향 이익 항로 성립: Coal 동행(8→11, +3/개), Machinery 서행(44→48, +4/개). 동일 항구 왕복은 여전히 손해. **빌드·검증 완료 — 첫 교역 루프(매입→항해→매도→이익) 동작.**
- HUD `PRT` 라인에 최근접 항구 이름 추가(`PRT LIVERPOOL E 620`) — 2항구 항법용, Phase 5 nearest-port HUD의 선행.
- **Phase 3 종결**: VoyageSave(OVYG v2) + 항구/화물/money + 입항·정박·출항 + 시장 매매 + 2항구 교역 루프. 농장 레거시 0. 다음은 ROADMAP Phase 4(렌더링 후속), 첫 항목은 기준 성능 측정.

### 2026-06-11 — 교역소 시장 매매 (ROADMAP Phase 3c-2)

- `MarketEntry { good; buyPrice; sellPrice; stock }` + BRISTOL 시장 5종(Coal 8/6, IronOre 12/9, Steel 30/24, Machinery 60/48, Grain 10/8 — buy>sell 스프레드로 동일 항구 왕복 매매는 손해). **빌드·동작 검증 완료.**
- 시장은 Docked 하위 상태: TRADE 클릭 → Up/Down 선택, B 구매 1, S 판매 1(Ctrl 제외로 Ctrl+S 저장과 무충돌), Esc로 항구 메뉴 복귀(이후 Esc는 pause). 검증: stock/money/적재공간/보유량.
- UI: `GOOD|BUY|SELL|STK|HELD` 표 + 선택 행 하이라이트. 렌더러는 `MarketRowHud` 표시값 배열만 복사(게임 무지 유지). `UI_MAX_VERTS` 8192→32768(시장 표 글리프 ~12k 버텍스).
- 한계(의도): 항구 재고(stock)는 save 미저장 — 로드 시 리셋. 재고 영속화는 save v3(ROADMAP Phase 9 save migration)에서.

### 2026-06-11 — 항구 입항·정박·출항 1차 (ROADMAP Phase 3c-1)

- `GameMode { Sailing, Docked }` 도입(AppMode 메뉴/일시정지와 별개). **빌드·동작 검증 완료.**
- 입항: 항구 반경(30m) 안 + 저속(≤2m/s)에서 `PRESS ENTER TO DOCK` 힌트 → Enter(edge-detect)로 정박(운동량 0, 물리 스킵, 부력은 렌더러 FFT 리드백이라 파도 위 부유 유지).
- 항구 메뉴: 화면 중앙 항구명 + `SET SAIL`/`TRADE`(3c-2까지 흐림 placeholder). 기존 pause 메뉴 rect+클릭 edge 패턴 재사용(`portMenuRowRect`, `consumePortMenuClick`). 일시정지 오버레이와 겹치지 않게 paused 중엔 숨김.
- 설계: 별도 정박 "씬" 없이 mode+UI만 — Phase 5 부두/등대 메시가 들어오면 같은 화면이 항구 풍경이 됨(대항해시대식 선착장→마을 트리는 이 메뉴의 항목 확장으로 수용). save에 mode 미저장(로드 시 항구 안 Sailing, Enter 재입항).
- 수정: `GameState.{h,cpp}`/`InputManager.cpp`(Enter)/`Types.h`/`VulkanContext.{h,_Frame.cpp}`/`main.cpp`.

### 2026-06-11 — 항구·화물 데이터 1차 + save v2 (ROADMAP Phase 3b)

- `Port { id, name, position, radius }`(시작 항구 BRISTOL, 초기 진행방향 200m 앞), `CargoGoodId` 5종/`CargoStack`/`CargoHold{capacity 100}`, money 1000. **빌드·동작 검증 완료.**
- save v2: money(음수 거부) + cargo stacks(enum 범위·count>0·합≤capacity 검증). v1은 버전 불일치 거부(개발 정책).
- HUD: `PRT 방위 거리`/`CRG used/cap`/`GLD money` + 반경 내 `NEAR PORT`. 렌더러는 `FrameRenderData` 표시값만 받음(게임 무지 유지). 벡터 폰트에 `/` 글리프 추가.
- 항구 시각 표현은 의도적으로 없음(Phase 5, 임시 마커 금지 원칙).

### 2026-06-11 — Player 미러 shim 제거 (Phase 1 잔재 완료)

- 농장 `Player` 클래스 완전 제거(`Player.h` 삭제) — ship을 미러링하던 임시 호환 shim의 소멸. **빌드·동작 검증 완료(시각 변화 0).**
- `GameState`: `m_player`/`player()` 제거 → `shipWorldPosition()` 추가(ship 2D 위치+deck 높이 1.0; 카메라 타깃·그림자 중심용, 부력은 GPU 리드백으로 실제 높이 재계산). `setShipState`는 단순 대입으로 인라인화.
- `main.cpp`: 카메라 추적·스냅·렌더러 입력 전부 `shipWorldPosition()` 직결. 프레임 순서(직전 프레임 위치 추적 → 물리 → 렌더) 동일.
- 이로써 농장 레거시 정리 완료. 남은 이름 잔재: `PlayerInput` 구조체명, CMake `PASTEL_DEV_BUILD` 옵션명(기능 무관).

### 2026-06-11 — World·Chunk·TerrainGen 완전 제거 (Phase 3 잔재 정리)

- 농장 월드 레거시 최종 삭제: `src/world/` 디렉터리(`World.{h,cpp}`/`TerrainGen.{h,cpp}`/`Chunk.h`) + CMake 등록 제거. **빌드·동작 검증 완료(시각 변화 0).**
- `main.cpp`: `World` 인스턴스·청크 스트리밍·`LOAD_RADIUS`/`UNLOAD_RADIUS` 제거. 세션 시작=VoyageSave 로드, 타이틀 복귀=`gameState` 리셋만.
- `Types.h`: 농장 타입 클러스터 118줄 제거(`TileType`/`ItemType`/헬퍼/`Vertex`/`InstanceData`/`ChunkVertex`/`TERRAIN_TEX_LAYERS`/`tileFaceLayer`/`ObjectInstance` — 전부 사용처 0 확인). 잔존: 메뉴 rect·`ShipVertex`·`UIVertex`.
- `ShipVertex` pad 제거(48→44B): pad는 `sizeof(ChunkVertex)` stride 호환용이었고, shadow 파이프라인 stride를 `sizeof(ShipVertex)`로 교체하며 존재 이유 소멸. ship 경로는 전부 `sizeof`/`offsetof` 기반이라 자동 추종 — 선박 렌더·그림자 정상 확인.
- 남은 레거시: `Player` 미러 shim(카메라가 사용), `GameState::day()`/`m_prevDay`(read 0), CMake `PASTEL_DEV_BUILD` 옵션명.

### 2026-06-11 — GameState 농장 잔재 제거 (Phase 3 잔재 정리)

- VoyageSave 전환으로 보존 이유가 사라진 GameState 농장 멤버 일괄 제거(-214줄). **빌드·동작 검증 완료.**
- 제거: `m_inventory`/`m_drops`/핫바/`m_inventoryOpen`/`m_nearWorkbench` + getter, `addItem/countItem/removeItem/craft`, `PlayerInput.toggleInventory/selectSlot`, I키·숫자키 바인딩(`InputManager`), `consumeInventoryEscape`(`main.cpp`), Types.h 농장 클러스터(`ItemStack`/`DroppedItem`/`INV_*`/`HOTBAR_SLOTS`/`Recipe`/`craftingRecipes`/`CRAFT_*`/`craftRowRect`).
- 의도된 동작 변화: I키·숫자키 무동작, 게임플레이 ESC는 항상 즉시 pause(인벤토리 닫기 단계 소멸). 스크롤 줌·항해·HUD는 무변경.
- 보류: `ItemType`/`TileType`/`isBlock/isTool/itemToTile/itemColor`는 `World::tryHarvestObject`/`Chunk`가 사용 중 → World 제거 슬라이스에서 일괄 정리.

### 2026-06-11 — 일시정지 UI 텍스트 잘림 수정 (UI_MAX_VERTS 2048→8192)

- 일시정지 화면에서 SETTINGS가 "SE"에서 끊기고 QUIT이 사라지는 버그 수정. **빌드·동작 검증 완료.**
- 원인: 벡터 폰트가 글리프 픽셀당 쿼드 1개(6버텍스)라 선박 HUD+PAUSED+메뉴 3행 동시 표시가 ~3k 버텍스 → `UI_MAX_VERTS`(2048) 초과분을 overflow 가드 `resize`가 잘라냄. Phase 2b 선박 HUD 추가 때부터 잠재, HUD 숫자 자릿수에 따라 간헐 재현(항해 중 ESC).
- 수정: `VulkanContext.h` 상수 1줄(2048→8192, 프레임당 ~192KB로 무시 가능). 버퍼는 `createUIBuffer`가 상수 기준 생성이라 추가 변경 없음.

### 2026-06-11 — 죽은 농장 세이브 배관 제거 (Phase 3 잔재 정리)

- VoyageSave 교체로 호출처 0이 된 농장 세이브 경로 제거. **빌드·동작 검증 완료(순수 삭제, 동작 변화 0).**
- 제거: `World::save/load` 정의 229줄+선언(`PFRM` 직렬화 전체 — `PFRM` 문자열이 src에서 소멸), `GameState::setPlayerPosition/setInventory/setDrops`, 고아 include(`World.cpp`의 cstring/fstream/filesystem, `World.h`의 string/array).
- 유지: `m_inventory`/`m_drops`/`craft`/청크 스트리밍 — 아직 살아있는 코드, 다음 슬라이스에서 정리.

### 2026-06-11 — VoyageSave(OVYG) 도입 (ROADMAP Phase 3a)

- 저장 책임을 `World::save/load`(농장 `PFRM`)에서 신규 `src/game/VoyageSave.{h,cpp}`로 이동. **빌드·동작 검증 완료(항해 상태 저장/복원, legacy 거부, 손상 파일 무크래시).**
- 포맷: magic `"OVYG"` + version 1. 필드: gameTime + ship position/velocity/heading/yawRate/throttle/rudder(float 9개). heading·velocity까지 복원되는 건 처음(기존엔 위치만).
- 검증·안전: atomic write(`.tmp`→rename, 실패 시 라이브 파일 보존), 전체 읽기 성공 시에만 commit, magic/version/finite-float 검증(NaN 전파 차단), throttle/rudder `[-1,1]` clamp. 구 `PFRM` save.dat은 magic 불일치로 거부 → 새 게임(개발 정책).
- `GameState::setShipState` 추가 — ship 전체 복원 + legacy Player 미러 즉시 동기화(카메라 스냅·청크 스트리밍이 로드 프레임에 복원 위치를 봄).
- 저장 안 함: timeOfDay(gameTime에서 계산), inventory/drops/청크(항해 게임에서 무의미), wake field/FFT phase(gameTime으로 재현).
- 죽은 코드化: `World::save/load`, `GameState::setPlayerPosition/setInventory/setDrops` 호출처 0 → 농장 레거시 정리 슬라이스에서 제거 예정.
- 수정: `main.cpp`(배선 교체, 고아 include 정리), `GameState.{h,cpp}`, `CMakeLists.txt`. 신규: `VoyageSave.{h,cpp}`.

### 2026-06-10 — 죽은 frustum 멤버 제거 (Phase 2d 후속 정리)

- 농장 청크/오브젝트 컬링에 쓰이던 `m_frustum`/`m_reflectionFrustum`이 해당 렌더 제거(2d-1~4) 후 write-only(대입만, read 0)로 남음 → 제거. **빌드·동작 검증 완료(시각 변화 없음).**
- 제거: 멤버 2개(`VulkanContext.h`), `#include "renderer/Frustum.h"`, `drawFrame`/`updateUniformBuffer`의 `Frustum::extractFrom` 대입 2개(`_Frame`).
- 유지: `Frustum.h`(Gribb-Hartmann 평면 추출 + AABB 컬링) — 향후 섬·항구 프러스텀 컬링용 휴면 유틸. `ubo.reflectionViewProj`는 GPU 전송분이라 유지.
- 수정: `VulkanContext.h`/`_Frame`.

### 2026-06-10 — 공유 씬 디스크립터에서 죽은 grass/terrain 제거 (ROADMAP Phase 2d-5b)

- 2d-5c에서 미뤘던 항목 완료. 공유 scene/reflection 디스크립터의 죽은 grass/terrain 텍스처 바인딩 제거. **빌드·동작 검증 완료(선박·하늘·그림자·반사·바다 정상, 화면 변화 없음).**
- 레이아웃: `createDescriptorSetLayout`에서 binding 2/3/4(grass·terrain·grassOpacity) 제거 → `{0,1,5,6,7}` 비연속 레이아웃(`bindings[8]→[5]`). **ship 텍스처는 binding 5/6/7 유지 → 살아있는 셰이더 0줄 수정**(Vulkan은 비연속 binding 허용).
- write: `createDescriptorSets`/`createReflectionDescriptorSets`의 grass/terrain image info + `writes[2..4]` 제거(`writes[8]→[5]`). 디스크립터 풀 이미지샘플러 `*14→*8`.
- 제거: `createGrassTexture`/`createTerrainTextureArray` 정의(절차 텍스처 생성 포함) + ctor 호출 + 소멸자 `destroy` + `m_grassTex`/`m_grassOpacityTex`/`m_terrainTex` 멤버·선언.
- 유지: `createTextureArray` 범용 헬퍼(미사용이 됐지만 향후 섬·항구 terrain 배열에 재사용 여지). 죽은 `m_frustum`/`m_reflectionFrustum`은 별도 슬라이스로 보류.
- 남은 고아: `TERRAIN_TEX_LAYERS`(`Types.h`) — `tileFaceLayer`/`TileType` 농장 레거시 타입 클러스터에 묶여 Phase 3 일괄 정리에 포함.
- 수정: `VulkanContext.{cpp,h}`/`_Init`.

### 2026-06-09 — dead 셰이더·빈 청크 TU CMake 정리 (ROADMAP Phase 2d-5c)

- 2d 제거로 죽은 셰이더를 CMake(컴파일 목록·복사 명령)에서 빼고 파일 삭제, 빈 `VulkanContext_Chunk.cpp`를 소스 목록·파일에서 제거.
- 삭제(10): `triangle.vert/.frag`(player 큐브), `chunk.vert/.frag`(청크·오브젝트 reuse), `object.vert`, `grass.vert/.frag`, `shadow_object.vert`, `shadow_grass.vert/.frag` + `VulkanContext_Chunk.cpp`.
- 유지: ui/ship/ocean*/`shadow.vert`/sky/post/smaa.
- 검증: 코드에서 삭제 셰이더 참조 0. **⚠️ CMake 변경이라 클린 빌드로 검증 권장.**
- **미완(2d-5b)**: 공유 디스크립터의 grass/terrain 텍스처(`m_terrainTex`/`createTerrainTextureArray`/`m_grassTex`/`m_grassOpacityTex`) 제거는 **디스크립터 레이아웃↔셰이더 바인딩 좌표가 얽혀** 있어 빌드 열고 별도 진행 권장(무해한 dead GPU 메모리). 죽은 `m_frustum`/`m_reflectionFrustum`도 함께 검토.
- 수정: `CMakeLists.txt`, 셰이더/소스 파일 10+1 삭제.

### 2026-06-09 — 렌더러-World 분리 완료 (ROADMAP Phase 2d-5a)

- **렌더러가 `World`를 완전히 모르게 됨(Phase 2d 핵심 달성).** 생성자 `VulkanContext(Window&, World&)` → **`VulkanContext(Window&)`**, `m_world` 멤버·world include·`class World;` 전방선언·dev UI 청크수 줄 제거. `main.cpp`는 `VulkanContext ctx(window)`.
- `VulkanContext.h`의 `world/Chunk.h` → `renderer/Types.h`(타입 유지, world 의존 제거). cpp/Frame/Init의 `world/World.h` 제거.
- `main.cpp`의 `world`는 청크 스트리밍·세이브용으로 유지(게임측).
- 검증: 렌더러에 `m_world`/`World&`/world include **0**. 빌드·동작 검증 완료.
- 남은 dead code: 공유 농장 텍스처(grass/terrain)+디스크립터 바인딩, dead 셰이더/CMake/빈 `_Chunk.cpp` → 2d-5b/5c.
- 수정: `VulkanContext.{cpp,h}`/`_Init`/`_Frame`, `main.cpp`.

### 2026-06-09 — 청크 메시 빌더·데이터 제거 (ROADMAP Phase 2d-4b)

- 2d-4a로 청크가 더 이상 그려지지 않게 된 뒤, 청크 메시 빌드/데이터 제거(191줄). **빌드·동작 검증 완료(화면 변화 없음).**
- 제거: `buildChunkBuffer`(내부 `m_world.getTile`), `rebuildDirtyChunks`(`m_world.chunks()`) → `VulkanContext_Chunk.cpp` 비워짐(주석만), `ChunkRenderData`/`m_chunkBuffers`, ctor/drawFrame/cleanup 호출, 선언.
- **결과: `m_world` 사용처가 3곳만 남음** — 생성자 `World&`, `m_world` 멤버, dev UI 청크수. → 2d-5에서 최종 제거.
- 수정: `VulkanContext.{cpp,h}`/`_Chunk`/`_Frame`.

### 2026-06-09 — 청크 voxel 메시 렌더링 제거 (ROADMAP Phase 2d-4a)

- 청크 메시는 물 월드(전부 WATER, 메셔가 스킵)에서 빈 메시 → 렌더링 제거(78줄). **빌드·동작 검증 완료(선박 그림자/반사 정상, 화면 변화 없음).**
- 제거: 청크 draw 3블록(shadow/reflection/scene), `createChunkPipeline` + `m_chunkPipeline`(생성/파괴/선언).
- shadow 패스 정리: 청크 제거로 미사용이 된 `lightFrustum` + 고아가 된 `m_shadowPipeline` 바인드(ship이 자체 재바인드) 제거. **ship 그림자 캐스터는 유지.**
- 유지: `buildChunkBuffer`/`rebuildDirtyChunks`/`m_chunkBuffers`/`ChunkRenderData`(빌드만 하고 미사용) → 2d-4b. reflection/scene의 dead descriptor 바인드는 sky 등 위해 유지.
- 수정: `VulkanContext.{cpp,h}`/`_Init`/`_Frame`.

### 2026-06-09 — 죽은 오브젝트 리소스 제거 (ROADMAP Phase 2d-3b)

- 2d-3a로 오브젝트가 더 이상 그려지지 않게 된 뒤, 죽은 오브젝트 리소스 제거(291줄). **빌드·동작 검증 완료(선박 정상 표시, 화면 변화 없음).**
- 제거: `createObjectMeshes`(나무/바위/울타리 메시 빌드), `createObjectPipeline`, `createShadowObjectPipeline` + ctor/cleanup 배선 + `m_objectPipeline`/`m_objectMeshes`/`m_shadowObjectPipeline` 멤버·선언.
- **ship 보존**: `createObjectMeshes` 끝의 `loadImportedShipMesh()` 호출을 ctor로 이전. `ObjectMesh` 구조체·`m_shipMesh`·`loadImportedShipMesh`는 유지.
- 수정: `VulkanContext.{cpp,h}`/`_Init`.

### 2026-06-09 — 오브젝트 dressing 렌더·빌더 제거 (ROADMAP Phase 2d-3a)

- 농장 오브젝트(나무/바위) dressing은 `placeTrees`/`placeRocks` 미호출(객체 0)인 물 월드에서 항상 빈 렌더 → 제거(117줄). **빌드·동작 검증 완료(화면 변화 없음).**
- 제거: object draw 3블록(shadow/reflection/scene), `buildChunkObjectBuffer`(호출+정의), `objectsDirty` 블록, `rebuildDirtyChunks` objGroups 정리, 선언, `ChunkRenderData::ObjGroup` 구조체 + `objGroups` 필드.
- renderer가 더 이상 `chunk.objectsDirty`를 읽지 않음(world/Chunk.h 필드는 게임측 유지).
- 유지: `m_objectMeshes`·object/shadowObject 파이프라인(죽었지만 생성/파괴만) → 2d-3b. `ObjectMesh` 구조체는 ship(`m_shipMesh`)이 사용하므로 유지.
- 수정: `VulkanContext.h`/`_Chunk`/`_Frame`.

### 2026-06-09 — 지면 dressing(ground patch/pebble) 렌더 제거 (ROADMAP Phase 2d-2)

- 농장 지면 dressing(ground patch + pebble)은 grass/dirt 타일이 0인 물 월드에서 항상 빈 렌더 → 제거(232줄). **빌드·동작 검증 완료(화면 변화 없음).**
- 제거: ground/pebble draw(reflection 블록은 object draw 유지, scene 블록 전체), `buildGroundDressingBuffer`(호출+정의, 내부 `m_world.getTile`), `grassDirty` dressing 블록(objectsDirty만 유지), `rebuildDirtyChunks` 정리, `ChunkRenderData`의 groundPatch/pebble 필드, `m_groundPatchMesh`/`m_pebbleMesh` + 메시 빌드.
- renderer가 더 이상 `chunk.grassDirty`를 읽지 않음(world/Chunk.h 필드는 게임측이라 유지).
- 수정: `VulkanContext.{cpp,h}`/`_Chunk`/`_Frame`/`_Init`.

### 2026-06-09 — 죽은 그래스 리소스 제거 (ROADMAP Phase 2d-1b)

- 2d-1a로 그래스가 더 이상 그려지지 않게 된 뒤, standalone 그래스 리소스를 제거(257줄). **빌드·동작 검증 완료(화면 변화 없음).**
- 제거: `createGrassPipeline`(+`m_grassPipeline`), grass card 메시(`uploadGrassCardMesh` 람다 + GRASS CARD 빌드 + `m_grassCardMesh`), shadow grass(`createShadowGrassPipeline`/`createShadowGrassDescriptors` + `m_shadowGrass*` 5종), `GrassCardVertex`.
- **유지**: `m_grassTex`/`m_grassOpacityTex` + `createGrassTexture` — 그래스 텍스처가 **공유 scene·reflection 디스크립터에 묶여 있어**(chunk/object가 같은 descriptor 사용) 지금 제거 불가 → chunk/object 렌더 제거(2d-4/5) 시 함께 정리.
- shaders `grass.*`/`shadow_grass.*`는 미사용이 됨(CMake/에셋 정리는 farm 렌더 제거 끝에 일괄).
- 수정: `VulkanContext.{cpp,h}`/`_Init`, `Types.h`.

### 2026-06-09 — 그래스 렌더링 제거 (ROADMAP Phase 2d-1a)

- 렌더러-`World` 분리(Phase 2d)의 첫 슬라이스. 농장 grass dressing은 grass 타일이 0인 물 월드에서 항상 빈 렌더 → 제거(189줄). **빌드·동작 검증 완료(화면 변화 없음).**
- 제거: 그래스 draw 3블록(shadow[비활성]/reflection/scene), `buildGrassDressingBuffer`(호출+정의, 내부 `m_world.getTile` 그래스 샘플 포함), `rebuildDirtyChunks`의 grassBuffer 정리, 선언, `ChunkRenderData`의 `grassBuffer/grassCount`.
- 남은 그래스 리소스(`m_grassPipeline`/`m_grassCardMesh`/`m_grassTex`/`m_shadowGrass*`)는 이제 죽은 채 생성/파괴만 됨 → 다음 슬라이스(2d-1b).
- 2d는 농장 voxel 지형·그래스·지면·오브젝트 dressing 렌더 일체(~600줄+, shadow/reflection/scene 패스 통합)를 들어내는 큰 작업이라 빌드 검증 가능한 슬라이스로 진행: **2d-1a(그래스 draw+dressing, 이 커밋)** → 2d-1b(그래스 리소스) → 2d-2(지면) → 2d-3(오브젝트) → 2d-4(청크 메시) → 2d-5(`rebuildDirtyChunks`+`m_world`/생성자 `World&` 제거).
- 수정: `VulkanContext.h`, `VulkanContext_Chunk.cpp`, `VulkanContext_Frame.cpp`.

### 2026-06-09 — 죽은 legacy 인스턴스 큐브 서브시스템 제거 (ROADMAP Phase 2a-3c)

- player 큐브/selector/drops가 모두 빠지며 아무것도 안 그리게 된 인스턴스 큐브 렌더 일체를 제거(5개 파일, ~136줄). **빌드·동작 검증 완료(화면 변화 없음).**
- 제거: `m_pipeline`(인스턴스 큐브 파이프라인) · 큐브 메시(`m_vertexBuffer`/`m_indexBuffer` + `createVertexBuffer`/`createIndexBuffer` + `kVertices`/`kIndices`) · player 큐브(`m_playerInstBuffer` + `createPlayerInstanceBuffer`/`updatePlayerInstanceBuffer` + `kPlayerColor`) — 생성·파괴·선언 전부.
- `createGraphicsPipeline()` → `createScenePipelineLayout()`로 개명하고 **공유 `m_pipelineLayout` 생성만 남김**(sky/chunk/object/grass/ship가 재사용 → 유지). grep로 제거 심볼 참조 0, 공유 레이아웃 12곳 정상 확인.
- `shaders/triangle.vert`/`.frag`는 미사용이 됐으나 에셋/CMake 정리는 별도(삭제 안 함).
- 수정: `VulkanContext.{h,cpp}`/`_Init`/`_Frame`/`_Private`.

### 2026-06-09 — drops 렌더 plumbing 제거 (ROADMAP Phase 2a-3b)

- 2c 이후 항상 비어 inert였던 드롭 아이템 렌더 경로를 **완전 제거**(5개 파일, ~74줄). **빌드·동작 검증 완료(화면 변화 없음).**
- 제거: `FrameRenderData.drops`+main 인자+dev UI Drops 줄, `updateDropInstanceBuffer`(호출/정의/선언), `createItemMesh`/`createDropInstanceBuffer`(정의/ctor 호출/cleanup/선언), 멤버 `m_itemVertexBuffer`/`m_dropInstBuffer`/`m_dropCount`/`MAX_DROPS`, drop draw + 그 앞의 고아 `m_pipeline` 바인드.
- 아이템 큐브 메시는 drops 전용이라 함께 삭제. 그 자리에 NOTE 주석: legacy 인스턴스 큐브 파이프라인(`m_pipeline`/player-cube)은 이제 아무것도 안 그림 → 이후 cleanup.
- 유지: `GameState`의 `m_drops`/`drops()`/`setDrops` + save/load drops(저장 포맷 묶임 → Phase 3에서 정리).
- 수정: `VulkanContext.{h,cpp}`/`_Init`/`_Frame`, `main.cpp`.

### 2026-06-09 — 타일 selector 서브시스템 제거 (ROADMAP Phase 2a-3a)

- 2c 이후 항상 inert였던 타일 selector를 코드까지 **완전 제거**(7개 파일, ~108줄 삭제). **빌드·동작 검증 완료(화면 변화 없음).**
- 제거: `FrameRenderData.targetTile`, `updateSelectorInstanceBuffer`(호출/정의/선언), `createSelectorBuffers`(호출/정의/선언)+cleanup, selector draw, 멤버(`m_selectorVertex/Index/InstBuffer`/`m_showSelector`), 메시 데이터(`kSelectorVertices/Indices`), `GameState`의 `m_targetTile`+`targetTile()`+orphan된 `#include <optional>`.
- `updateSelectorInstanceBuffer`가 쓰던 `m_world.tileCenter` 의존이 함께 사라짐 → 2d(렌더러-World 분리) 선행.
- 곁다리: stale가 된 `// Player / selector` 파이프라인 주석 → `// Player / drops`로 정정.
- 공유 `m_pipeline`·큐브 메시·drops·player 경로는 유지. 수정: `VulkanContext.{h,cpp}`/`_Init`/`_Frame`/`_Private`, `main.cpp`, `GameState.h`.

### 2026-06-09 — 죽은 농장 HUD 필드 제거 (ROADMAP Phase 2a-2)

- 2b로 *write-only*가 된 농장 HUD 데이터 경로를 렌더 데이터에서 제거. **빌드·동작 검증 완료(게임 화면 변화 없음).**
- `FrameRenderData`에서 `hotbarSelected`/`inventory`/`inventoryOpen`/`day`/`nearWorkbench` 5개 필드 제거. `main.cpp` 인자·`drawFrame` 미러 대입·죽은 멤버(`m_hotbarSelected`/`m_invHud`/`m_inventoryOpen`/`m_dayHud`/`m_nearWorkbenchHud`) 함께 제거.
- dev UI(`buildDevUi`): 농장 표시(Day/Selected slot/Near workbench) → 선박 상태(Heading/Thr/Rud)로 교체.
- 인벤토리/드롭은 `GameState` getter·save/load 경로로는 유지(렌더 입력에서만 분리). 남은 selector(`targetTile`)/drops 렌더 plumbing은 2a-3에서.
- 수정: `VulkanContext.h`, `VulkanContext_Frame.cpp`, `main.cpp`.

### 2026-06-09 — 농장 HUD → 선박 HUD (ROADMAP Phase 2b)

- 게임플레이 농장 HUD(hotbar·인벤토리 격자·제작 패널·day 숫자)를 좌상단 **선박 HUD**로 교체. **빌드·동작 검증 완료.**
- 표시: `SPD`(속도) / `HDG`(deg, 0~359) / `THR`(F전진·R후진 %) / `RUD`(S우현·P좌현 %). 글리프에 마이너스가 없어 부호를 방향 문자로 표시.
- 배선: `FrameRenderData`에 `shipThrottle`/`shipRudder` 추가, `main.cpp` 전달, HUD 미러 멤버(`m_shipSpeed/Heading/Throttle/RudderHud`) 추가·`drawFrame` 대입(속도=`length(shipVelocity)`). 기존 `pushText`/`pushNumber` 벡터 폰트 재사용(새 의존성 없음).
- 메뉴/설정/로딩/일시정지 UI는 그대로. 농장 HUD 미러(`m_invHud`/`m_hotbarSelected`/`m_dayHud`/`m_nearWorkbenchHud`)는 이제 대입만 되고 안 읽힘 → 다음 단위(2a-2)에서 `FrameRenderData` 필드와 함께 제거.
- 수정: `VulkanContext.h`, `VulkanContext_Frame.cpp`, `main.cpp`.

### 2026-06-09 — 농장 상호작용 제거 + GameState 디커플링 (ROADMAP Phase 2c)

- `GameState::update`에서 농장 상호작용 3종 제거: 드롭 줍기, 인접 작업대 감지(`nearWorkbench`), 마우스 ray 타일 피킹(`targetTile`).
- `m_targetTile`을 더 이상 set하지 않아 `nullopt` 유지 → 렌더러의 타일 selector가 자동 inert(수면 위 노란 선택자 박스 사라짐). 렌더 plumbing·`FrameRenderData` 필드는 유지(다음 단위에서 제거).
- 이제 안 쓰므로 `update` 시그니처에서 `Camera`/`World` 인자 제거 → `update(dt, input)`. `GameState.cpp`의 `world/Camera` include·`GameState.h` 전방선언 제거. **GameState 게임 루프가 World/Camera에 더 이상 의존하지 않음**(Phase 2d 렌더-World 분리의 선행).
- 수정: `src/game/GameState.{h,cpp}`, `src/main.cpp`.

### 2026-06-09 — 렌더 입력 이름 정리: player* → ship* (ROADMAP Phase 2a-1)

- `FrameRenderData`의 `playerPosition/Velocity/Heading` → `shipPosition/Velocity/Heading`. 렌더 입력이 "player"가 아니라 "ship"을 의미하게. **빌드·동작 검증 완료(행동 불변).**
- `main.cpp`가 이 값을 `gameState.ship()`에서 **직접** 공급 → 기존의 속도 차분(`(pos-posBefore)/dt`)·`atan2(facing)` heading 추출 hack 제거. 정확한 `ship.velocity`/`ship.heading` 사용(wake 입력의 1프레임 지연 제거).
- 소비처 갱신: dev UI 라벨(`Player:`→`Ship:`), 그림자 center, wake 입력, 부력(`updateShipTransform`). 의미만 정리.
- `Player` 미러는 유지(카메라·청크 스트리밍·세이브가 아직 사용). 수정: `VulkanContext.h`, `VulkanContext_Frame.cpp`, `main.cpp`.

### 2026-06-09 — 기본 선박 상태와 항해 물리 (ROADMAP Phase 1)

- 농장 `Player` 위치로 선박을 흉내 내던 구조를 끝내고, 관성·선회반경을 가진 `ShipState` 기반 항해로 교체. **빌드·체감 검증 완료.**
- **`ShipState`**(position/velocity/heading/yawRate/throttle/rudder)를 `GameState`가 소유, `ship()` getter 추가.
- **입력 의미 교체**: WASD를 카메라상대 타일-워크에서 throttle(W/S)·rudder(A/D)로 재해석. `InputManager`/`PlayerInput` 무변경(기존 필드 재사용).
- **항해 물리 1차**(`updateShipPhysics`): 전진 추진 + 선형 드래그 + 속도 제한(전진 9 / 후진 2, 비대칭) + 속도 의존 선회(정지 시 제자리 회전 불가) + yaw 감쇠 + 미세속도/yaw 스냅. 튜닝 상수는 익명 namespace에 모음.
- **호환 미러링**: `Player`를 임시 shim으로 두고 매 update 끝에 ship→player(position/facing)를 미러. 덕분에 `main.cpp`·렌더러 무수정으로 카메라·wake(`m_oceanWakeShip*`)·부력(`updateShipTransform`)·그림자 center·청크 스트리밍·세이브가 선박을 따라감.
- `setPlayerPosition`이 ship도 동기화 → 로드/텔레포트 위치가 미러에 덮어써지지 않음. (heading 저장은 Phase 3 VoyageSave에서.)
- 농장 카메라상대 이동·타일 충돌(`canOccupy`) 제거. `Player::moveBy/moveSpeed`는 미사용으로 shim에 잔존(Player 제거 시 함께 정리).
- 수정 범위: `src/game/GameState.h`, `src/game/GameState.cpp` 두 파일만.
- 다음: Phase 2(`FrameRenderData` `player*`→`ship*` 이름 정리, 농장 HUD→선박 HUD, 렌더-`World` 분리).

### 2026-06-06 — 그래픽 레퍼런스 문서화 (`docs/RENDERING_REFERENCES.md`)

- AAA 사실적 해양 구현에 쓸 **기법·논문·엔진 문서·오픈소스 라이브러리·참고 게임**을 신규 `docs/RENDERING_REFERENCES.md`로 정리.
- 구성(13개 섹션): 참고 게임 → 해양·물(Tessendorf FFT·Horvath 스펙트럼·Bruneton ocean BRDF·GPU Gems·keithlantz 튜토리얼) → PBR(selfshadow 코스·Filament·PBR Book) → 반사(Stochastic SSR) → TAA(Karis 2014·Playdead INSIDE) → 그림자(CSM/PCSS) → 대기/구름(Hosek-Wilkie·Bruneton·Hillaire·Nubis) → 톤매핑(Hable·Narkowicz·AgX) → 식생/바람 → **선박 부력·항해 물리(Kerner)** → **라이브러리·오픈소스** → Vulkan API → 종합 허브.
- **신규 섹션 2개**(2차 보강):
  - §10 선박 부력·항해 물리 — Jacques Kerner "Water interaction model for boats"(hydrostatic+동적 힘). 우리 Phase 1 부력/Phase 6~7 선체 hydrodynamics에 직접 연결.
  - §11 라이브러리·오픈소스 — VMA/volk/SPIRV-Reflect(Vulkan, Phase 9), KTX-Software/meshoptimizer/cgltf(에셋, Phase 8), Crest/GarrettGunnell·Water/gasgiant·FFT-Ocean(해양 학습 코드). 도입은 승인+라이선스 정책 명시.
- **URL 검증 완료**: WebFetch로 핵심 링크 직접 확인. 수정: Playdead TAA repo는 `playdeadgames/temporal`(과거 표기 temporalAA는 404), GPU Gems Ch.1 정확 딥링크 적용, 죽은 WaveWorks GitHub 링크 제거(상용·비공개로 표기). 일부 논문/GDC 발표는 제목 검색으로 안내.
- **문서 scope 분리 명시**: `RENDERING_REFERENCES`=무엇을 구현/누구를 보고 배우나(기법·논문·라이브러리·게임), `VULKAN_REFERENCES`=Vulkan으로 어떻게 배선하나(API 패턴). 상호 보완.
- 각 항목을 ROADMAP Phase에 매핑. 교차 참조 추가: `VULKAN_REFERENCES`(상호 보완), `DESIGN`(Visual North Star), `ROADMAP`(Phase 4).

### 2026-06-06 — 개발 로드맵 문서화 (`docs/ROADMAP.md`)

- AI 브레인스토밍으로 뽑은 장기 개발 순서(10개 묶음, 약 580개 세부 항목)를 프로젝트 문서 체계에 맞게 정리.
- **신규 `docs/ROADMAP.md`**: Phase 0~10으로 정제 압축. 근거리(Phase 1~4)는 작업·검증·닿는 파일까지 상세, 원거리(Phase 5~10)는 가볍게. 중복 제거.
  - Phase 1 항해 물리 → Phase 2 농장 제거/렌더 경계 → Phase 3 VoyageSave/교역 1차 → Phase 4 렌더 후속(TAA 등) → Phase 5~7 세계/경제/항해 심화 → Phase 8 비주얼·에셋 → Phase 9 기술 부채/구조 → Phase 10 완성도.
  - 핵심 결정: **항해 물리가 TAA/async보다 먼저**, async compute는 마지막. 첫 작업 단위 = `ShipState` + WASD throttle/rudder + 관성 이동 1차.
- **전체 동기화**: 문서 역할 경계를 지키며 교차 참조로 연결.
  - `README.md`: 현재 개발 목표에 ROADMAP/Phase 1 포인터.
  - `DESIGN.md`(안정 닻 유지): 헤더·MVP에 ROADMAP 포인터만.
  - `docs/ARCHITECTURE.md`: §7.1 신규 데이터 구조 스케치(ShipState/ShipDef/CargoHold/Port/Island/Wind/VoyageSave), §14.1 엔진 구조 안정화, §14에 ROADMAP 매핑.
  - `docs/ENGINE_TODO.md`: P3에 엔진 구조·디버깅·테스트(파일 분리/descriptor 정리/debug label/pure logic 테스트/save migration/품질 tier/legacy 제거) 추가, ROADMAP Phase 4/9 연결.
  - `docs/MIGRATION_PLAN.md`: 범위를 "농장 전환(Phase 0~5)"으로 명확화, 이후 게임 구축은 ROADMAP로. Phase 3 → ROADMAP Phase 1 티켓 포인터.
  - `docs/RUN_CHECKLIST.md`: §9.1 ROADMAP Phase별 점검(도입 시 활성) 추가.
- 코드 변경 없음. 문서 전용이라 빌드/실행 점검 불필요.

### 2026-06-06 — 프로젝트 문서 최신화

- README/DESIGN/ARCHITECTURE/ENGINE_TODO/MIGRATION_PLAN/CODE_CLASSIFICATION/VULKAN_REFERENCES/RUN_CHECKLIST를 현재 코드 기준으로 정리.
- 핵심 정정:
  - 해양 렌더링은 다중 캐스케이드 FFT·SSR/플래너 반사·CSM·PBR·wake 시뮬레이션까지 구현된 상태로 명확화.
  - 게임플레이는 아직 농장 `Player`/타일 충돌 이동을 선박처럼 렌더링하는 상태이며, `ShipState`/항해 물리가 다음 핵심 작업임을 명확화.
  - 기본 월드 생성은 물 타일 기반 해상 테스트 상태지만, `World`/`Chunk`/`TileType`/인벤토리/핫바/저장 포맷 구조가 남아 있음을 문서화.
  - SMAA는 edge pass에서 톤매핑 luma를 쓰지만 neighborhood는 HDR scene color를 섞은 뒤 톤매핑하므로, 완전한 tone-map 후 SMAA 구조는 아직 아님을 정정.
  - displacement 리드백 크기를 512²×3 RGBA16F = 약 6.0 MiB/프레임으로 정정.
- 빌드/실행 점검은 수행하지 않음. 사용자가 다음 빌드에서 `docs/RUN_CHECKLIST.md` 기준으로 확인.

### 2026-06-06 — 항적(wake) 시뮬레이션 + 조명/선박 머티리얼

- **선박 항적을 시뮬레이션 마스크로 구현** (화면 도색이 아님). `shaders/ocean_wake.comp` + `VulkanContext_Ocean.cpp`의 `createOceanWake`/`recordOceanWake`.
  - world-locked 토로이달 R16F 마스크를 프레임 간 ping-pong(frame-in-flight별 이미지)으로 유지. 채널 = `r` foam / `g` turbulence / `b` signed height / `a` churn.
  - 매 프레임 이류(advection)·확산·감쇠 후 선박 입력 주입: Kelvin arm(tan 19.47°), prop wash, bow shoulder/fan, hull shear, 선체 half-width 프로파일 16샘플(`m_shipHullProfile`).
  - `ocean.vert`는 wake height + choppiness를, `ocean.frag`는 foam/노멀 섭동을 샘플. 거리 페이드로 원거리 안정화.
- **중앙 흰 리본 아티팩트 제거** + foam 생성 위치를 스턴/현측/bow로 재분배.
- **조명 + 선박 머티리얼 보강**: `ship.frag` PBR을 specular 맵 기반 roughness/F0, 달빛 specular, 하늘광 ambient/반사, 흘수선(wetline) 처리까지 확장. 낮/밤·달 조명 경로 정리. 빛 보간(day/night 전이) fix.

### 2026-06-05 — 깊이 기반 물 · SSR · CSM · 선박 PBR

- **pre-water scene color/depth 복사 패스 분리**: 불투명 패스를 끝낸 뒤 색·깊이를 복사하고, 별도 water 패스에서 물이 이를 샘플 → 실제 씬 깊이 기반 굴절·흡수(Beer-Lambert)·얕은 물 처리. `copySceneColorForWater`/`copySceneDepthForWater`.
- **화면공간 반사(SSR)**: 28스텝 레이마치 + 5스텝 이분 refinement + temporal reprojection(이전 프레임 색/깊이 history rejection). 플래너 반사·분석적 하늘과 confidence 가중 합성. 여러 커밋에 걸쳐 1차→안정화→refinement로 발전.
- **CSM(3 캐스케이드)**: 뷰 프러스텀 슬라이스 바운딩 스피어 fit + 텍셀 스냅(shimmer 제거) + 캐스케이드 블렌드 밴드 + Poisson 16탭 PCF. (Phase 2까지)
- **선박 PBR 에셋**: LSV018 선체 모델 + albedo/normal/specular DDS 텍스처 로드(`createDDSBC1Texture`).
- 기타: displacement+slope 맵 더블버퍼(컴퓨트/그래픽스 오버랩), 메뉴/로딩 시 FFT 디스패치 게이팅, Jacobian whitecap seed를 `ocean.frag` foam coverage로 연결, 디바이스/스왑체인 적합성 검사, Dev 이동 속도 조절.

### 2026-06-04 (후반) — 농장→바다 전환 + FFT 해양 스택 구축

> 이 날 같은 날짜의 앞 항목(원칙 재명문화/세이브 무결성/그림자·풀)은 농장 시기 작업이고, 아래는 그 뒤 진행된 바다 전환 작업이다.

- **전환 1단계**: 농장 게임플레이 행동 비활성화, 카메라를 UWO식 배 추적 시점으로, 월드를 평평한 바다 기준점으로, 플레이어 큐브를 임시 선박(선체·돛·그림자)으로 교체.
- **해수면을 Gerstner 1차에서 다중 캐스케이드 Tessendorf FFT로 발전**. GPU 컴퓨트 체인: 초기 스펙트럼 h0(k) 생성 → per-frame 스펙트럼 애니메이션 H(k,t) → butterfly IFFT(log2N 수평+수직) → displacement/slope 조립. 512² × 3 캐스케이드. `VulkanContext_Ocean.cpp`, `shaders/ocean_spectrum*.comp`/`ocean_fft.comp`/`ocean_assemble.comp`.
- **해수면 셰이딩**: 픽셀 단위 FFT 노멀(slope 맵 샘플) + 수평 변위(choppiness), GGX 태양 반사·윤슬, 하늘/태양 반사 + 플래너 반사(평면 클리핑), 다중 스케일 타일 노멀맵 디테일, HDR(R16F) + ACES 톤매핑, 원거리 LOD 방사형 메시 + 장거리 대기 fog, procedural sky 배경 패스.
- **선박 부력**: FFT displacement를 host로 리드백하고 수평 변위를 역산해 선박을 실제 파면 위에 부유·틸트.
- 정리: 위험한 spectrum 튜닝과 임시/저품질 foam 제거, Jacobian 기반 whitecap seed 도입.

### 2026-06-04 — RTX 3060 / AAA급 해양 렌더링 원칙 재명문화

- 반복적으로 저사양 임시 기법이나 화면 도색식 fake 효과가 들어가는 문제를 막기 위해 프로젝트 최상위 개발 원칙을 다시 명문화했다.
- 1원칙: OceanVoyage의 모든 개발과 구현은 **RTX 3060급 하드웨어에서 AAA/AA급 사실적 해양 게임을 만든다**는 기준을 따른다.
- 2원칙: 실제 상용 게임과 엔진에서 쓰이는 **안정적이고 확장 가능한 표준 기법**을 우선한다. Vulkan 정공법, 수명/동기화 안정성, 최종 렌더러로 성장 가능한 구조를 선택 기준으로 삼는다.
- 금지: 화면에 흰 선을 그리는 wake, 저해상도 노이즈 foam, 타일 블롭, flat color stand-in, "일단 보이게만" 하는 저품질 셰이더 꼼수.
- wake/foam의 올바른 방향: 선박/파도/해안 입력을 별도 mask나 simulation target에 기록하고, advection/decay/diffusion을 거쳐 ocean shader가 샘플하는 구조. 단순 fragment shader 도색은 폐기한다.
- `AGENTS.md`, `claude.md`, `README.md`, `DESIGN.md`, `docs/ARCHITECTURE.md`에 같은 원칙을 반복 기재했다.

### 2026-06-04 — 세이브 무결성 + 견고성 (Phase 0)
> 외부 LLM(Codex) 리뷰를 코드로 검증해 "렌더 품질보다 세이브/진행도 견고성이 더 시급"으로 우선순위를 재배치한 결과. 상세 진단은 `ARCHITECTURE` "게임 견고성·데이터 무결성".

- **세이브 v2→v3**: `World::save/load`에 인벤토리(27슬롯)·드롭·타일별 `watered` 직렬화 추가. `GameState::setInventory/setDrops` 추가, `main.cpp` save/load 배선. 재시작 시 제작·채집·씨앗·물 준 상태가 유지된다(이전엔 전부 손실).
- **Atomic write**: `save.dat.tmp`에 쓰고 flush/close 후 `std::filesystem::rename`으로 교체 → 저장 중 크래시가 기존 세이브를 파손하지 못함.
- **로드 검증 + 무변경 보장**: magic/ver + `count`/`objCount`/`dropCount` 범위 + `ItemType`/`ObjectType` enum 범위 검사. 모든 데이터를 로컬에 읽고 **전체 성공 시에만 커밋**(손상/절단 세이브가 월드·인벤을 부분 변경하지 못함). 버전 불일치(v2 등) = 새 월드(개발 정책).
- **dt clamp**: `main.cpp`에서 프레임 dt를 `0.1s`로 상한 → stall/resize/디버거 정지 후 시간·성장·이동 점프 + 충돌 터널링 방지.
- **작물 성장 catch-up**: `growthTick`을 람다로 추출해 `m_chunks` + `m_modifiedUnloaded`를 모두 틱. 물-게이트 모델(물 준 날만 1단계, 매일 마름)이라 day-delta가 아니라 "두 맵 모두 틱"이 정답 — unloaded 동안 재물주기 불가라 물 1회분 1단계만 성장.
- 다음: Phase 1 렌더 correctness (albedo `*_SRGB` / mask UNORM 분리, 밉맵 + trilinear + anisotropic).

### 2026-06-03 — 그림자 품질 패스 + 품질/정공법 원칙 명문화

**렌더링 방향 리셋 (문서/메모리):** 매 세션 저사양 워크어라운드로 회귀하던 문제를 구조적으로 차단.
- 당시에는 권장 1660 Super 예산으로 고품질 기본값을 문서화했으나, 이 기준은 2026-06-04 OceanVoyage RTX 3060 / AAA-AA급 사실적 해양 렌더링 원칙으로 대체됐다.
- 현재 기준은 `AGENTS.md`/`CLAUDE.md`/`README.md`/`DESIGN.md`/`docs/ARCHITECTURE.md`/`VULKAN_REFERENCES.md`의 RTX 3060 원칙을 따른다. "Simplicity First"는 *코드 구조*에만 적용되고 *렌더링 품질·표준성*엔 적용 안 됨을 못박음.
- AAA 2.5D 관행 교차검증으로 ARCHITECTURE에 **"2.5D 고품질 잔디·룩 레시피"** 결정 블록 추가: 잔디 셰이딩 스택(개체 색 랜덤 → height gradient → base AO → translucency/backlight → wind → fade), 그림자 정책(캐릭터/나무/오브젝트만 실시간 shadow, 잔디는 받기만·안 쏨 + ground contact AO로 그라운딩), terrain blending, GPU-driven은 *규모상* 보류.

**그림자 떨림(shimmering)/acne 작업 (uncommitted):**
- texel snapping: 라이트 직교 투영을 월드 원점 기준 텍셀 격자에 스냅(`VulkanContext_Frame.cpp`). translation 떨림 방지용 표준 stabilization. 단독 효과는 미미 — 잔디·돌 떨림의 주원인은 해상도/얇은 그림자였음.
- light frustum 축소 **range 80→45**: fog(57) 밖은 안 보이므로 실효 해상도 ~1.8배. **나무 그림자 떨림 확실히 개선.**
- soft PCF **3×3→5×5** (`chunk`/`grass`/`triangle.frag`, texel 1/2048 유지).
- **grass shadow 캐스터 비활성화** (`kGrassCastsShadow=false`): 얇은 풀 그림자는 단일 shadow map에서 ~1텍셀이라 sun sweep에 깜빡임 + PCF로 못 잡음. AAA 2.5D 표준대로 잔디는 cast 안 하고 ground contact AO로 그라운딩하는 방향 결정. 파이프라인/셰이더 완전 제거는 cleanup 예정.
- 빌드 관찰: 나무 그림자 개선 / 잔디·돌은 잔디 캐스터 제거로 떨림 사라짐(단 그라운딩이 휑함 → 다음 트랙에서 보강).

**다음 세션:** 트랙 ① 잔디 리얼리즘(translucency/backlight + 색·gradient → ground contact AO) → ② 텍스처 mipmap/이방성/terrain blending → ③ 그림자 해상도 4096/CSM+soft·소품 contact shadow → ④ 흰 화면 → ⑤ post(bloom/SSAO). 상세는 `ARCHITECTURE` "렌더 품질 결함" + "2.5D 고품질 잔디·룩 레시피".

### 2026-06-03 — 풀 텍스처, blade-field, grass shadow 실험
- 풀 렌더링을 기존 절차 alpha texture 중심에서 외부 foliage atlas 기반으로 확장했다.
  - 기본 경로: `assets/textures/vegetation/grass_blades/color.png`
  - opacity mask 경로: `assets/textures/vegetation/grass_blades/opacity.png`
  - 두 파일 중 하나만 있거나 크기가 다르면 런타임 에러로 처리한다.
  - 파일이 없으면 기존 `assets/textures/grass.png` 또는 절차 fallback으로 동작한다.
- grass descriptor를 Color/Opacity 분리 구조로 바꿨다.
  - 기존 grass color sampler와 별개로 opacity sampler를 binding 4에 추가했다.
  - 일반 grass fragment shader는 opacity mask로 alpha test를 수행한다.
  - 향후 normal/roughness/AO 같은 foliage material map을 추가할 때 color atlas를 다시 repack하지 않아도 된다.
- Foliage006 계열 atlas에 맞춰 grass card UV rect를 직접 지정했다.
  - 큰 clump 반복감을 줄이기 위해 낮고 촘촘한 blade-field cluster 형태를 유지한다.
  - 풀 scale/density를 한 차례 낮춰 과한 해초 느낌을 완화했다.
  - shader tint, root darkening, wind sway, distance fade도 함께 조정했다.
- 시각 실험 중 풀 아래 fake contact patch를 잠깐 추가했으나 실패로 판단하고 제거했다.
  - 별도 `grassContact` 메쉬/버퍼/렌더링은 화면에서 그림자가 아니라 동그란 얼룩처럼 보여 부적합했다.
  - 이 경로는 현재 코드에 남기지 않았다.
- 풀 카드 자체를 shadow pass에 넣는 실험을 추가했다.
  - 새 셰이더: `shaders/shadow_grass.vert`, `shaders/shadow_grass.frag`
  - CMake shader 목록과 post-build copy에 `shadow_grass.*`를 등록했다.
  - grass shadow pipeline은 opacity mask를 샘플해 투명 부분을 `discard`하고, 가까운 청크만 shadow map에 depth를 쓴다.
  - 현재 결과는 실제 그림자이긴 하지만, 화면에서는 길쭉한 붓자국/얼룩처럼 읽혀 어색하다.
  - 다음 세션에서 1순위로 판단할 것: grass shadow를 강하게 제한할지, 풀 전체 shadow caster를 끄고 바닥/풀 셰이딩 쪽으로 우회할지.
- 빌드는 사용자 담당이라 이 세션에서는 실행하지 않았다. 정적 확인으로 `git diff --check`만 통과했다.

다음 세션 권장 판단:
- 현재 `shadow_grass` 실험은 그대로 두고 먼저 높이/알파/거리/확률 제한을 강하게 걸어 본다.
- 그래도 길쭉한 얼룩이 남으면 grass shadow pass를 제거하고, grass root shading + ground grass texture/detail + 낮은 대비 material breakup으로 방향을 바꾼다.
- 현재 OceanVoyage 기준에서는 RTX 3060급 예산을 사용한다. 화면에 티 나는 가짜 얼룩이나 과도한 개별 풀 그림자는 목표 품질과 맞지 않는다.

### Vulkan 초기화 + 첫 삼각형
- GLFW 창 생성 (`Window` 클래스, RAII 방식)
- Vulkan Instance, Validation Layer, Surface, Physical/Logical Device 초기화
- Swapchain, Image Views, Render Pass, Graphics Pipeline 구성
- Framebuffer, Command Pool/Buffer, Sync Objects 생성
- 셰이더 하드코딩 삼각형 출력

### Vertex Buffer
- `Vertex` 구조체 정의 (`pos: vec2`, `color: vec3`)
- `kVertices` 데이터를 CPU → GPU 메모리로 업로드
- 파이프라인에 Vertex Input 바인딩 정보 등록
- `vkCmdBindVertexBuffers` + `vkCmdDraw`로 그라데이션 삼각형 출력

### World 클래스 분리
- `src/world/World.h` / `World.cpp` 신규 생성
- 타일 데이터(`m_grid[H][W]`)와 색상 테이블을 VulkanContext에서 분리
- `VulkanContext`가 `World&` 참조를 받아 `createInstanceBuffer()`에서 읽도록 변경
- `main.cpp`에서 `World` 생성 후 `VulkanContext`에 전달
- 역할 분리: World = 게임 데이터, VulkanContext = 렌더링

### 플레이어 이동
- 플레이어 전용 인스턴스 버퍼 1개 (persistently mapped) 별도 생성
- `processInput()`에서 WASD로 플레이어 이동, 매 프레임 버퍼 업데이트
- 카메라 각도(orbitAngle) 기준으로 forward/right 방향 계산
  - `forward = (-cos θ, -sin θ)` — 카메라 반대 방향(화면 안쪽)
  - `right = (-sin θ, cos θ)` — 화면 기준 오른쪽
- 플레이어 드로우콜을 타일 드로우콜과 별도로 호출 (같은 파이프라인, 다른 인스턴스 버퍼)
- `updateUniformBuffer()`에서 `m_orbitTarget = playerPos`로 카메라가 플레이어를 따라감
- 플레이어 색상: 주황색 `{1.0, 0.45, 0.1}`

### Player/GameState 분리
- `src/game/Player.h` 추가 — 플레이어 위치와 이동 속도 보관
- `src/game/GameState.h` / `GameState.cpp` 추가 — 입력 스냅샷(`PlayerInput`) 기반 플레이어 이동 계산
- `VulkanContext`에서 `m_playerPos`와 `processInput()` 제거
- `main.cpp`가 GLFW 입력을 읽고 `GameState::update(dt, input, orbitAngle)` 호출
- `VulkanContext::drawFrame(playerPosition)`으로 플레이어 위치만 전달
- 렌더러 역할 축소: 플레이어 상태를 소유하지 않고, 전달받은 위치로 카메라 타겟/플레이어 인스턴스 버퍼만 갱신
- 결과: 화면과 조작은 유지하면서 게임 로직과 렌더링 책임 분리

### 기본 이동 충돌
- `World::inBounds(x, y)` 추가 — 월드 범위 밖 좌표 차단
- `World::isWalkable(x, y)` 추가 — 현재는 `WATER` 타일을 이동 불가로 처리
- `World::worldToTile(position)` 추가 — 플레이어의 연속 좌표(float)를 타일 좌표(int)로 변환
- `World::tileCenter(x, y)` 추가 — 타일 렌더링 위치와 게임 로직 좌표 기준을 `World`에서 통일
- `GameState::update()`가 이동 후보 위치를 계산한 뒤 `World`에 보행 가능 여부를 질의
- X/Y축을 나눠서 이동 검사 — 한 축이 막혀도 다른 축 이동은 가능한 구조
- `VulkanContext::createInstanceBuffer()`가 직접 오프셋을 계산하지 않고 `World::tileCenter()` 사용
- 결과: 플레이어가 맵 밖으로 나가지 않고, 물 타일 위로 이동하지 않음

### 타일 그리드 시스템
- `TileType` enum 추가 (GRASS, DIRT, WATER, STONE)
- `Vertex`에서 color 제거 — 색상은 인스턴스에서 담당
- `InstanceData`에 `color` 추가 (`pos` + `color`)
- `kWorld[10][10]` 배열로 타일 맵 정의
- 타일 타입 → 색상 변환 후 인스턴스 버퍼에 업로드
- 셰이더: `instanceColor`를 base color로 사용, Lambert 조명 적용
- 결과: 잔디/흙/물/돌이 섞인 플랫 셰이딩 타일 맵

### 인스턴싱
- `InstanceData { glm::vec3 pos }` 구조체 추가
- 인스턴스 버퍼 생성 (10×10 그리드 위치 데이터)
- 파이프라인에 binding 1 추가 (`VK_VERTEX_INPUT_RATE_INSTANCE`)
- 셰이더: `layout(location=3) in vec3 instancePos` → `worldPos = inPosition + instancePos`
- `vkCmdDrawIndexed(indexCount, instanceCount=100, ...)` — 드로우콜 1번
- 그리드 원점 중심 정렬: 각 위치를 `x - (GRID-1)/2` 로 오프셋
- 경계 타일 옆면 saw-tooth 현상은 타일 시스템 구현 시 내부 면 제거로 해결 예정

### 플랫 셰이딩
- `Vertex`에 `normal` 필드 추가
- 큐브 정점 8개 → 24개 (면당 4정점, 법선 공유 불가)
- 셰이더에 `flat` qualifier — 삼각형 내 보간 없이 단일 색상
- Fragment shader: Lambert 조명 `ambient(0.3) + diffuse(0.7) × dot(normal, lightDir)`
- 면별 색상: 윗면 밝은 초록 / 옆면 중간·어두운 초록 / 아랫면 갈색
- 결과: 로우폴리 타일 스타일 큐브

### 궤도 카메라
- 자유 시점 카메라 → 궤도 카메라로 교체
- `m_orbitAngle`, `m_orbitDistance`, `m_orbitPitch` 로 카메라 위치 계산
- Q/E로 `m_orbitAngle` 증감 → 타겟 주위 공전
- `updateUniformBuffer()`에서 구면 좌표 → 데카르트 좌표 변환 후 `lookAt`
- 게임 방향: Don't Starve 스타일 고정 시점, 추후 플레이어 위치를 `m_orbitTarget`으로

### Depth Buffer + 3D 큐브
- `createDepthResources()` — depth image/memory/view 생성 (DEVICE_LOCAL, D32_SFLOAT)
- `createImage()` 헬퍼 추가
- Render Pass에 depth attachment 추가 (attachment 1번)
- Framebuffer에 depth image view 연결
- Pipeline에 `VkPipelineDepthStencilStateCreateInfo` 추가 (depthTest/Write = true, compareOp = LESS)
- `cleanupSwapchain` / `recreateSwapchain`에 depth 리소스 포함
- `Vertex.pos` vec2 → vec3, 셰이더도 동일하게 업그레이드
- `kVertices` 4개 → 큐브 8개, `kIndices` 6개 → 36개
- 결과: 원근감+depth 정렬이 정상 작동하는 회전 3D 큐브

### UBO + MVP 행렬
- `UniformBufferObject` 구조체 정의 (model / view / proj mat4)
- Descriptor Set Layout, Descriptor Pool, Descriptor Set 생성
- Uniform Buffer를 프레임마다 (MAX_FRAMES_IN_FLIGHT=2) 각각 생성, 영구 매핑
- `updateUniformBuffer()` — 매 프레임 GLM으로 MVP 계산 후 memcpy
  - model: 시간에 따라 Z축 회전
  - view: `glm::lookAt({2,2,2}, {0,0,0}, {0,0,1})`
  - proj: `glm::perspective(45°, aspect, 0.1, 10.0)` + Y 반전
- `recordCommandBuffer`에서 `vkCmdBindDescriptorSets`로 셰이더에 연결
- frontFace를 `COUNTER_CLOCKWISE`로 수정 (Y반전으로 winding order가 뒤집혀서)
- 결과: 원근감 있게 기울어진 사각형이 회전

### Index Buffer
- 정점 4개 + `kIndices {0,1,2, 0,2,3}`으로 사각형 구성
- `createIndexBuffer()` 추가
- `vkCmdDraw` → `vkCmdBindIndexBuffer` + `vkCmdDrawIndexed`
- 결과: 그라데이션 사각형 출력

### 마우스 피킹 (Raycasting) 및 사거리 제한
- 화면의 2D 마우스 좌표를 3D 월드 공간으로 변환하는 레이캐스팅(Raycasting) 구현
- 역산환(Unprojection) 과정:
  1. 마우스 픽셀 좌표를 NDC(-1.0 ~ 1.0) 공간으로 변환
  2. 투영(Proj)과 뷰(View) 행렬의 역행렬(`inverse(proj * view)`)을 계산
  3. NDC 좌표에 역행렬을 곱해 월드 공간의 Ray 방향 벡터(Direction) 도출
- 수학적 교차 판정: - 타일이 모두 $Z=0$ 평면에 있다는 점을 이용, 루프 없이 O(1) 수학 공식($t = -O_z / D_z$)으로 광선과 바닥이 만나는 정확한 3D 좌표를 단번에 계산
- 사거리 제한 (Clamp):
  - 마우스가 아무리 멀리 있어도 플레이어 주변 타일만 선택되도록 처리
  - `std::clamp`를 이용해 타겟 타일과 플레이어 타일 간의 거리(delta)를 X, Y 각각 -1 ~ 1 사이로 강제 고정
- 기존의 키보드 방향 기반 타겟팅(`updateTargetTile`) 로직을 완전 제거하고 마우스 조작으로 일원화

### 카메라 구조 리팩토링 (Camera 클래스 분리)
- `Camera` 클래스 신규 생성: 카메라의 위치, 타겟, 뷰(View) 및 투영(Projection) 행렬 계산 로직 캡슐화
- **DRY 원칙 적용:** `GameState`와 `VulkanContext` 양쪽에 중복되어 있던 행렬 계산 코드를 제거하고, `Camera` 객체의 참조를 받아 사용하도록 통합
- **의존성 분리:** - 렌더러(`VulkanContext`)는 카메라 상태 변수(`orbitAngle` 등)를 소유하지 않고 넘겨받은 행렬만 렌더링에 사용
  - 게임 로직(`GameState`)은 단순 각도가 아닌 카메라의 실제 3D 좌표를 기반으로 플레이어 이동 방향(`forward`, `right`)을 역산하도록 구조 개선
- `main.cpp`에서 카메라 객체를 소유하며, 창 리사이즈 이벤트 발생 시 카메라의 종횡비(Aspect Ratio)를 실시간 갱신하도록 처리하여 화면 찌그러짐 방지

### Staging Buffer 도입 (VRAM 최적화)
- 정적 데이터(큐브 정점, 큐브 인덱스, 셀렉터 정점/인덱스)를 CPU 접근 가능 메모리(`HOST_VISIBLE`)에서 GPU 전용 초고속 메모리(`DEVICE_LOCAL`)로 마이그레이션.
- `copyBuffer` 헬퍼 함수 구현: 임시 버퍼 생성 → 데이터 복사(`vkCmdCopyBuffer`) → 동기화(`vkQueueWaitIdle`) → 임시 버퍼 파괴로 이어지는 정석적인 Vulkan 전송 파이프라인 구축.
- 매 프레임 업데이트가 필요한 `m_instanceBuffer`, `m_playerInstBuffer` 등은 `HOST_VISIBLE`을 유지하여 불필요한 복사 오버헤드 최소화.

### 입력 시스템 분리 (InputManager)
- `src/platform/InputManager` 클래스를 신규 생성하여 플랫폼 종속적인 입력 처리(GLFW)를 전담하도록 구조 개선.
- `main.cpp`의 메인 루프에 하드코딩되어 있던 키보드, 마우스 좌표, 창 크기 계산 로직을 `pollInput()` 메서드 하나로 캡슐화.
- **메인 루프 다이어트:** 길고 지저분했던 입력 수집 코드를 1줄로 압축하여 가독성을 크게 높임.
- 추후 커스텀 단축키 설정(Key Binding)이나 UI 클릭 시 월드 클릭 무시 같은 기능을 추가할 때 확장이 용이해짐.

### copyBuffer 동기화 개선 (VkFence)
- `copyBuffer` 내 `vkQueueWaitIdle` → `VkFence` 기반 동기화로 교체.
- `vkQueueWaitIdle`은 큐에 제출된 **모든 작업**이 끝날 때까지 블로킹 — 렌더링 중 호출 시 GPU 완전 정지.
- `VkFence`를 `vkQueueSubmit`에 넘기고 `vkWaitForFences`로 **이 전송 하나**만 대기 → 다른 큐 작업 불간섭.
- 현재는 초기화 전용 호출이므로 동작 차이 없음. 청크 런타임 로드/언로드 시 이 구조가 필수.

### World 3D 그리드 전환
- `TileType::AIR = 0` 추가 — 0으로 메모리 초기화 시 전체 AIR가 되어 3D 배열 초기화에 편리.
- `m_grid[HEIGHT][WIDTH]` → `m_grid[DEPTH][HEIGHT][WIDTH]` (`DEPTH = 8`).
- 생성자: `memset`으로 전체 AIR 초기화 후, Z=0 레이어에만 기존 맵 데이터를 `memcpy`로 배치.
- `getTile / setTile / inBounds / isWalkable / tileCenter` 전부 Z 파라미터 추가.
- `worldToTile` 반환 타입 `glm::ivec2` → `glm::ivec3`. Z 계산: `round(position.z) - 1` — 플레이어가 타일 위 1유닛 높이에 서는 구조를 반영.
- `isWalkable`: AIR와 WATER 둘 다 이동 불가로 처리.
- `GameState` / `VulkanContext` 전파: `glm::ivec2` → `glm::ivec3`, 레이캐스팅 `delta.z = 0`으로 타겟은 플레이어와 같은 Z 레이어로 고정.
- `createInstanceBuffer`: Z 루프 추가, AIR 타일 스킵 — 비어있는 레이어는 자동으로 렌더링 제외.
- 현재 동작 변화 없음 — Z=1~7은 전부 AIR. 이후 `setTile(x, y, z, type)` 호출만으로 블록 배치/파괴가 바로 연결됨.

### 청크 시스템 (16×16)
- 고정 배열 `m_grid[DEPTH][H][W]` → `unordered_map<ivec2, Chunk>` — 무한 확장 가능한 구조로 전환.
- `Chunk.h` 신규 생성: `CHUNK_SIZE=16`, `CHUNK_DEPTH=8`, `Chunk` 구조체(`tiles`, `dirty` 플래그), `TileState`(농경지 성장 단계 등 미래 상태 예약), `IVec2Hash`.
- 타일 좌표 = 월드 좌표 직접 매핑 (`tileCenter(x,y,z) = {x,y,z}`) — 기존 중앙 정렬 오프셋 제거, 플레이어 시작 위치 `{5,5,1}`(맵 중앙)으로 변경.
- `World::getTile/setTile`: 청크 좌표(`chunkCoord`) + 로컬 좌표(`localCoord`)로 라우팅. 미로드 청크는 AIR 반환.
- `setTile` 호출 시 해당 청크 `dirty=true` 자동 마킹.
- **렌더러 청크 버퍼**: 단일 `m_instanceBuffer` → `unordered_map<ivec2, ChunkRenderData>` (청크당 버퍼 1개).
- `rebuildDirtyChunks()`: 매 프레임 dirty 청크만 버퍼 재빌드 → 블록 변경 시 전체가 아닌 해당 청크만 GPU 업로드.
- `recordCommandBuffer`: 청크 맵을 순회하며 청크당 draw call 1번 — 청크 단위 프러스텀 컬링 기반 마련.
- `inBounds`: X/Y 무한 확장 대응으로 Z 범위만 검사.

### Frustum Culling (청크 단위)
- `renderer/Frustum.h` 신규 생성 — GLM만 의존하는 독립 구조체.
- **Gribb & Hartmann 방법**: `viewProj` 행렬 행 조합으로 6개 평면(left/right/bottom/top/near/far)을 O(1)에 추출. 정규화 생략 — 부호 판정만 필요하므로 불필요한 sqrt 없음.
- `containsAABB(min, max)`: 각 평면에 대해 "positive vertex"(법선 방향으로 가장 먼 꼭짓점)를 구해 평면 바깥이면 즉시 `false` 반환 — 최악 6번 dot product.
- 청크 AABB: `min={cx*16, cy*16, 0}`, `max={(cx+1)*16, (cy+1)*16, 8}`.
- `drawFrame`에서 `camera.viewProj()`로 매 프레임 frustum 갱신, `recordCommandBuffer`에서 각 청크 draw call 전 AABB 테스트 → 시야 밖 청크는 draw call 자체가 발생하지 않음.
- 현재 청크가 (0,0) 하나뿐이라 실측 효과 없음 — 청크 수가 늘어나면 즉시 작동.

### 청크 사이즈 확장 (16→32) 및 맵 확대
- `CHUNK_SIZE 16 → 32` — 청크당 타일 수 256 → 1024 (4배). 로딩 반경 3청크 기준 96×96 → 192×192 타일.
- 마인크래프트 청크(16×16) 대비 4배 크기 — 농사 게임 특성상 플레이어가 넓은 농장 안에서 활동하므로 청크당 타일 수가 많을수록 청크 경계를 덜 넘어 dirty 재빌드 빈도 감소.
- 초기 맵 10×10 → 32×32 전체로 확대. 좌상단 물 코너, 두 곳 흙 패치(잠재적 농지), 두 곳 돌 패치(채굴 영역), 나머지 잔디.
- 플레이어 시작 위치 `{5,5,1}` → `{15,15,1}` (32×32 맵 중앙).

### 윗면/옆면 색상 분기 (top/side color)
- `InstanceData`에 `sideColor` 추가 — 기존 `color` → `topColor + sideColor` 두 채널.
- `World::tileSideColor(TileType)` 추가 — GRASS는 흙 갈색, DIRT는 짙은 갈색, STONE은 짙은 회색, WATER는 짙은 파랑.
- Vulkan pipeline vertex input: attribute 4개 → 5개 (`instanceSideColor` location=4 추가).
- `buildChunkBuffer`: `tileColor` + `tileSideColor` 함께 인스턴스 버퍼에 기록.
- vertex shader: `instanceTopColor / instanceSideColor` 두 색상 fragment로 전달.
- fragment shader: `step(0.9, fragNormal.z)`로 윗면 판별 → `mix(sideColor, topColor, isTop)`으로 색상 선택.
- 텍스처 전환 시 `topColor/sideColor` → `topTexId/sideTexId`로 교체만 하면 되는 구조 — 호환성 확보.

### Hidden Face Culling + 청크 메시 생성
- **인스턴싱 → 청크별 동적 메시**로 전환 — 인접 타일이 있는 면은 버텍스 버퍼에 추가하지 않음.
- `ChunkVertex { pos, normal, color }` 신규 구조체 — color는 윗면이면 topColor, 옆/아랫면이면 sideColor로 빌드 시 구워짐.
- 6면 정의 테이블(`kFaces`) — 각 면의 4개 로컬 정점 오프셋, 법선, 이웃 오프셋(neighbor check 방향), isTop 플래그를 정적 배열로 정의.
- `buildChunkBuffer`: 각 타일 6방향 이웃 타일을 `World::getTile`로 조회 → AIR이면 해당 면의 버텍스 4개+인덱스 6개 추가, AIR이 아니면 스킵.
- 인덱스 타입 `uint16` → `uint32` — 32×32×8 청크 최악 케이스 294,912 인덱스로 uint16 한계(65535) 초과.
- `chunk.vert` / `chunk.frag` 신규 셰이더 — 인스턴스 속성 없이 per-vertex color 사용, fragment는 Lambert만.
- `m_chunkPipeline` 신규 파이프라인 — binding 1개(ChunkVertex), no instancing. 기존 `m_pipeline`(플레이어/셀렉터 인스턴싱)과 공존.
- `recordCommandBuffer`: 청크는 `m_chunkPipeline`으로 draw, 이후 `m_pipeline`으로 전환하여 플레이어/셀렉터 draw.
- 청크 `ChunkRenderData`: 단일 버퍼 → `vertexBuffer + indexBuffer` 쌍으로 교체.

### 다단계 지형 초기 맵
- `World.cpp` 생성자에 Z=1, Z=2 레이어 추가 — 나머지 시스템(Hidden Face Culling, top/side 색상, 3D 그리드) 변경 없음.
- 행별 x 범위 테이블(`Row { y, x0, x1 }`)로 불규칙한 타원형 언덕 정의 — Z=1 13개 행, Z=2 7개 행.
- Z=1 언덕: 맵 상단 중앙부, Z=2 정상: Z=1 안쪽 더 작은 면적. Z=2 아래엔 반드시 Z=1이 있어 부유 타일 없음.
- 결과: 절벽 옆면 갈색(sideColor), 정상 초록(topColor), Hidden Face Culling으로 내부 면 자동 제거.

### 3D-Aware Collision
- `canOccupy` in `GameState.cpp`: added body-height check — destination tile at `z+1` must be AIR in addition to `isWalkable(x, y, z)`.
- Previously only the ground tile (Z=0) was checked; player could walk through elevated blocks at Z=1/Z=2.
- Step-up logic (auto-climb 1 block) and slope movement deferred to later gameplay pass.

### Chunk Load/Unload + Procedural Terrain
- `TerrainGen.h/cpp` (new): deterministic hash-based FBM noise (4 octaves, no external library). `generate(cx, cy, chunk)` fills a chunk procedurally.
- Terrain rules: Z=0 always solid (GRASS or DIRT by biome noise), Z=1 hill where height > 0.45, Z=2 stone peak where height > 0.65.
- Two noise channels: `HEIGHT_SCALE=1/32` for elevation shape, `BIOME_SCALE=1/24` for GRASS/DIRT distribution.
- `World::loadChunksAround(cx, cy, radius)` — generates unloaded chunks within radius on demand.
- `World::unloadChunksOutside(cx, cy, radius)` — erases chunks beyond radius from `m_chunks`.
- `VulkanContext::rebuildDirtyChunks()` — before rebuilding, frees GPU buffers for chunks no longer in `m_world.chunks()`.
- `main.cpp` — detects player chunk change each frame; load radius=3, unload radius=4.
- Handcrafted initial map removed; `World` constructor is now empty — all terrain generated on demand.

### Block Placement/Destruction
- `PlayerInput::rightClick` added; `InputManager` polls `GLFW_MOUSE_BUTTON_RIGHT`.
- `GameState::update` signature changed from `const World&` to `World&` to allow tile mutation.
- Left click → `world.setTile(target, TileType::AIR)` (destroy). Right click → `world.setTile(target.xy, z+1, TileType::STONE)` (place on top).
- Target tile Z improved: after XY raycast hit, scans from `CHUNK_DEPTH-1` down to find topmost non-AIR tile — elevated blocks are now selectable. `delta.z` clamped ±1 instead of forced 0.
- GPU sync fix: `vkDeviceWaitIdle` called before destroying chunk buffers in `buildChunkBuffer` and before unloaded-chunk cleanup in `rebuildDirtyChunks` — prevents VUID-vkDestroyBuffer-buffer-00922 when tile changes trigger immediate buffer rebuild.

### Terrain Variety (Water / Trees / Biome)
- `TileType` extended: `WOOD`, `LEAVES` added (indices 5, 6); color + side-color tables updated to match.
- Water: tiles where height noise < 0.28 become `WATER` at Z=0 with nothing above (low basins / lakes).
- Biome channel (`BIOME_SCALE = 1/24`): drives GRASS vs DIRT ground and tree density. Dry biome → DIRT patches.
- Trees (`placeTrees`): on flat GRASS in forest biome (b > 0.58), sparse hash threshold (> 0.90). Trunk WOOD at Z=1/Z=2, 3x3 LEAVES canopy at Z=3, single LEAVES top at Z=4.
- Trunk kept ≥2 tiles from chunk edge so the canopy fits inside the chunk — avoids cross-chunk writes (chunks generate independently).
- NOTE: voxel trees are a placeholder. Organic props (trees/crops/rocks) will move to a separate low-poly model layer; terrain stays voxel.

### Hotbar UI (custom quad rendering)
- Dedicated UI pipeline (`m_uiPipeline`) — separate from 3D: depth test/write off (always on top), alpha blending on, cull none, no descriptor sets (vertices already in NDC).
- `UIVertex { vec2 pos; vec4 color }`, `ui.vert`/`ui.frag` (passthrough + Lambert-free flat color).
- `m_uiPipelineLayout` is empty (no descriptors) — UI is fully screen-space.
- `m_uiBuffer`: persistently mapped, rebuilt each frame in `updateHotbar`. Capacity 256 verts (panel + slots + highlight ~66).
- `updateHotbar`: pixel→NDC conversion using current swapchain extent → slots stay correct size regardless of aspect. Centered bottom: panel bg, yellow highlight behind selected slot, 9 slot fills.
- Drawn last in `recordCommandBuffer`, before `vkCmdEndRenderPass`.

### Hotbar Block Selection
- `Window`: scroll callback + `consumeScrollY()` (accumulate yoffset, read-and-reset).
- `InputManager` refactored from `GLFWwindow*` to `Window&` so it can read scroll. Polls number keys 1..9 (`GLFW_KEY_1 + i`) and scroll direction.
- `PlayerInput`: `selectSlot` (-1 or 0..8) and `scrollDelta` (±1) added.
- `GameState`: owns `m_selectedSlot` + `m_palette[9]` (GRASS/DIRT/STONE/WOOD/LEAVES/WATER, rest AIR). Number key sets slot, scroll wraps modulo. Right-click places `palette[selected]` instead of hardcoded STONE (AIR slots = no-op).
- `HOTBAR_SLOTS = 9` moved to `Types.h` as shared constant.
- `drawFrame` takes `(hotbarSelected, palette)`; `updateHotbar` draws each slot's block color as an inner icon via `World::tileColor`.

### Ambient Occlusion (per-vertex voxel AO)
- Standard 0fps voxel AO: each face vertex checks its 3 corner neighbors (2 edge-adjacent + 1 diagonal) in the air layer adjacent to the face; more occluders → darker.
- AO level 0..3 maps to brightness `{0.5, 0.7, 0.85, 1.0}`, multiplied into the vertex color at build time — no new vertex attribute, no extra render pass.
- Shader change: `fragColor` switched from `flat` to smooth interpolation so per-vertex AO blends across the face; `fragNormal` stays `flat` (constant per face, used for Lambert). Vertices aren't shared across faces, so no cross-face color bleed.
- Generic per-face computation: tangent axes derived from the face normal (the two axes where normal is 0), corner direction from the vertex's local position sign.
- **Caching (perf):** AO sampling would otherwise call `World::getTile` (hashmap lookup) ~hundreds of thousands of times per chunk → startup hitch. Fixed by building a padded `(CHUNK_DEPTH+2)×(CHUNK_SIZE+2)²` local copy once per chunk: interior copied straight from `chunk.tiles`, only the 1-tile border ring hits `getTile` (~3.4k vs ~hundreds of thousands). Face-cull and AO then index the local array. Confined to `buildChunkBuffer`; `World` untouched.

### Object Layer (low-poly tree models)
- Trees moved off the voxel grid into a separate **object/prop layer** — terrain stays voxel, organic props become low-poly models. Reused later for crops/rocks/items.
- `Chunk` gains `std::vector<Object> objects` (`Object { pos, scale, rot, type }`); `TerrainGen::placeTrees` now pushes tree objects instead of writing WOOD/LEAVES voxels. WOOD/LEAVES tile types kept as player-placeable building blocks.
- Tree mesh (`createTreeMesh`): box trunk + 3 stacked cones (pine), flat-shaded, built once into a shared vertex buffer. Reuses `ChunkVertex` (pos/normal/color).
- Instanced rendering: `ObjectInstance { pos, scale, rot }`, per-chunk instance buffer in `ChunkRenderData`. `object.vert` applies per-instance Z-rotation + scale; fragment shader reused from `chunk.frag`. Scale/rotation varied per tree via hash (less repetition).
- Dedicated `m_objectPipeline` reuses `m_pipelineLayout` (same UBO descriptor). `cullMode = NONE` — procedural cone/box winding isn't guaranteed outward-facing, so draw both sides (overdraw negligible for small meshes).
- Drawn per chunk after chunk meshes, reusing the existing frustum-cull AABB test.
- NOTE: trees no longer block movement (objects have no collision). Deferred to Phase 3 (tool system / object interaction).

### In-game Time System
- `DAY_DURATION = 120.0f` seconds per in-game day (adjustable constant in `GameState.h`).
- `GameState` gains `m_time` (total elapsed seconds), `m_day` (int, increments each full day), `m_timeOfDay` (0.0 = midnight → 0.5 = noon → 1.0 = midnight). All three updated in `update()` each frame.
- `day()` and `timeOfDay()` accessors exposed for the farming system (crop growth checks against `m_day`).
- Sky clear color in `VulkanContext` now lerps across 4 keyframes keyed on `timeOfDay × 4`: midnight (dark navy), dawn (pink/orange), noon (sky blue), dusk (orange/red). Stored in `m_skyColor[4]`, set in `drawFrame()`, read in `recordCommandBuffer()`.
- No dynamic lighting change yet — terrain brightness stays constant. Ambient light tie-in deferred to Phase 6 (rendering polish).

### Item/Tool System + Inventory UI
- `ItemType` enum added to `Types.h`: `NONE`, `BLOCK_GRASS/DIRT/STONE/WOOD/LEAVES/WATER`, `TOOL_HOE`, `TOOL_AXE`, `COUNT`. Inline helpers: `isBlock()`, `isTool()`, `itemToTile()`, `itemColor()`.
- Inventory layout constants (`INV_COLS=4`, `INV_ROWS=2`, `INV_SLOT_SIZE`, `INV_GAP`, `INV_PAD`) in `Types.h` — shared between `GameState` (click detection) and `VulkanContext` (rendering) so the math stays in sync.
- `GameState`: hotbar palette changed from `TileType[9]` to `ItemType[9]`. Default slots: 6 block types + HOE + AXE + NONE. Right-click with a tool selected does nothing (actual tool actions added in Phase 3-6). World interaction (click/raycast) suppressed while inventory is open.
- `I` key toggles inventory; edge-detected with `m_prevToggleInv` to avoid repeated fires on hold.
- Inventory click: mouse position checked against each slot rect using the shared layout constants. Matching slot's `ItemType` written to `m_palette[m_selectedSlot]`.
- `VulkanContext`: `updateHotbar()` now reads `ItemType` and calls `itemColor()` directly — no more `World::tileColor` in the UI path. Inventory open → full-screen dim quad + panel background + 4×2 item grid rendered on top of hotbar. UI vertex buffer expanded from 256 to 512.

### Farming System (Farmland / Seeds / Growth)
- New `TileType`: `FARMLAND` (dark moist brown), `WHEAT` (growth-stage color).
- New `ItemType`: `SEED_WHEAT` (replaces the last NONE slot in the default palette).
- **HOE right-click** on GRASS/DIRT → replaces that tile with FARMLAND (same Z, in-place tilling).
- **SEED_WHEAT right-click** on FARMLAND → places WHEAT at Z+1, initializes `TileState { growthStage=0, lastUpdatedDay=currentDay }`.
- **Growth**: `World::growthTick(currentDay)` called once per in-game day from `GameState`. Iterates all loaded WHEAT tiles; if `currentDay - lastUpdatedDay >= 2`, increments `growthStage` (max 3) and marks chunk dirty.
- **WHEAT color** is growth-stage-aware: `World::tileColor(TileType, uint8_t growthStage)` returns pale green → yellow-green → yellow → golden. `buildChunkBuffer` reads `chunk.states[z][ly][lx].growthStage` for WHEAT tiles.
- **Harvest**: left-click on any WHEAT → AIR (existing destroy logic). FARMLAND below is unaffected.
- **Walkability**: `isWalkable` excludes WHEAT (crop is not solid ground). `canOccupy` treats WHEAT as passable for the body-height check so the player can walk over farmland with growing crops.

### World Save / Load
- Binary format (`save.dat`): magic `"PFRM"` + version byte + player position (3×float) + game time (float) + chunk count + per-chunk data.
- Per chunk: `cx/cy` (int32×2) + raw `tiles[8][32][32]` (8 192 B) + `growthStage[8][32][32]` (8 192 B) + `lastUpdatedDay[8][32][32]` (32 768 B) ≈ 49 KB per chunk.
- Only **modified** chunks are saved. `Chunk::modified` is set by `setTile` / `setTileState`; procedural generation writes directly to `chunk.tiles` and does not set the flag.
- `m_modifiedUnloaded` map: when `unloadChunksOutside` would erase a modified chunk, it moves it here instead. `loadChunksAround` checks this map before calling `TerrainGen` — so modified chunks survive unload/reload cycles without hitting disk.
- On startup, `world.load()` pre-populates `m_modifiedUnloaded`; `loadChunksAround` then picks those up in place of fresh generation.
- Ctrl+S triggers `world.save()`; no auto-save yet (planned for Phase 5 settings screen).
- Object layer (tree instances) is not saved — trees are deterministically re-generated by `TerrainGen`, so no data loss.

### Optimization Pass
**growthTick catch-up fix**
- `if` → `while` in `World::growthTick`. Previously, crops in a chunk that had been unloaded (moved to `m_modifiedUnloaded`) could only advance one growth stage on the first tick after reloading, regardless of how many days had passed. The while loop now drains all pending stages in one tick: `while (growthStage < 3 && currentDay - lastUpdatedDay >= GROWTH_DAYS)`.

**growthTick visual update fix**
- `bool changed = false` was declared in `growthTick` but never set to `true` inside the while loop — `if (changed) chunk.dirty = true` never fired.
- Crops advanced in memory (growthStage incremented, lastUpdatedDay updated) but the chunk mesh was never re-queued for GPU rebuild, so the color change from green → golden never appeared on screen.
- Fix: add `changed = true` inside the while loop body.

**Deferred deletion queue (vkDeviceWaitIdle removal)**
- Added `DeferredDelete { VkBuffer, VkDeviceMemory, uint64_t frame }` and `m_deletionQueue` + `m_frameCount` to `VulkanContext`.
- `deferDestroy(buf, mem)` pushes a buffer onto the queue tagged with the current frame.
- `drawFrame()` increments `m_frameCount` and flushes queue entries older than `MAX_FRAMES_IN_FLIGHT` frames — at that point the GPU is guaranteed to have finished reading them.
- `buildChunkBuffer()`: removed `vkDeviceWaitIdle` + immediate `vkDestroyBuffer`; replaced with two `deferDestroy` calls for vertex and index buffers.
- `rebuildDirtyChunks()`: removed `vkDeviceWaitIdle` + immediate destroy for unloaded chunk buffers; replaced with `deferDestroy` for vertex, index, and object-instance buffers.
- Destructor flushes the remaining queue after `waitIdle()`.
- Eliminates full GPU pipeline stalls on every tile change and chunk unload. Most impactful when `growthTick` dirties multiple chunks simultaneously on a day transition.
### VulkanContext 파일 분리 (리팩토링)
- `VulkanContext.cpp` (~1716줄) → 4개 .cpp + 1개 비공개 헤더로 분리. 클래스 인터페이스(`VulkanContext.h`) 변경 없음.

| 파일 | 내용 |
|------|------|
| `VulkanContext.cpp` (~275줄) | 생성자/소멸자, waitIdle, deferDestroy, createBuffer, copyBuffer, createImage, cleanupSwapchain, recreateSwapchain 등 공유 헬퍼 |
| `VulkanContext_Init.cpp` (~970줄) | 모든 초기화 `create*` 함수 (device, swapchain, pipeline, descriptor, buffer 생성 등) |
| `VulkanContext_Frame.cpp` (~264줄) | drawFrame, recordCommandBuffer, updateUniformBuffer, updateHotbar, updateSelectorInstanceBuffer |
| `VulkanContext_Chunk.cpp` (~173줄) | buildChunkBuffer, buildChunkObjectBuffer, rebuildDirtyChunks |
| `VulkanContext_Private.h` (~107줄) | 파일 간 공유 상수/타입 (kVertices, kIndices, UniformBufferObject, kEnableValidation, debug helpers) |

- 분리 기준: Init = 시작 시 1회 호출, Frame = 매 프레임, Chunk = 월드 지오메트리. 헬퍼 함수는 Init/Frame/Chunk 양쪽에서 쓰이므로 core 파일에 유지.
- `buildChunkObjectBuffer`에서 `vkDestroyBuffer`를 즉시 호출하던 버그 수정 → `deferDestroy()`로 교체. GPU가 아직 읽는 도중 버퍼를 파괴해 `VUID-vkDestroyBuffer-buffer-00922` validation error + 블록 설치/파괴 직후 크래시가 발생하던 문제 해결.

### Dynamic Sun Lighting
- `UniformBufferObject`에 `vec4 lightDir` 추가 (xyz = 태양 방향, w = dayFactor 0..1). 기존 192 bytes → 208 bytes.
- `updateUniformBuffer()`가 `timeOfDay`를 받아 매 프레임 태양 위치 계산: `elevation = sin(tod × π)` (자정=0, 정오=1), `azimuth = tod × 2π` (하루 동안 360° 회전). sunDir은 두 값으로 구성한 단위 벡터.
- Descriptor set layout의 UBO stage flags: `VERTEX_BIT` → `VERTEX_BIT | FRAGMENT_BIT`. Fragment shader에서 UBO를 읽으려면 필수.
- `chunk.frag` / `triangle.frag`에 UBO 바인딩 추가. Lambert diffuse에 `dayFactor` 곱해 낮엔 full lighting, 밤엔 diffuse=0. ambient는 당시 `mix(0.15, 0.3, dayFactor)`로 보간 — 이후 hemisphere ambient 튜닝에서 밤 바닥값을 0.10으로 낮춤.
- `chunk.vert`, `object.vert`, `triangle.vert` UBO 구조체에 `vec4 lightDir` 선언 추가 (C++ 쪽 버퍼 크기와 일치).
- 결과: 시간 흐름에 따라 태양 방향이 회전하고 밤이 되면 어두워짐. Shadow Map의 light matrix 기반이 되는 단계.

### Shadow Map Infrastructure
- Shadow map 전용 Vulkan 리소스 생성 (`createShadowResources()`). 스왑체인과 무관하게 한 번만 생성되며 리사이즈 시 재생성 불필요.
- `VkImage` (1024×1024, depth format, `DEPTH_STENCIL_ATTACHMENT | SAMPLED` usage) + `VkDeviceMemory` + `VkImageView` (depth aspect).
- Shadow 전용 `VkRenderPass`: color attachment 없이 depth attachment 1개만. `loadOp=CLEAR`, `storeOp=STORE`, `finalLayout=DEPTH_STENCIL_READ_ONLY_OPTIMAL` — 이후 main pass의 fragment shader가 샘플링할 수 있는 상태로 전환.
- Subpass dependency 2개: (1) 이전 프레임의 shadow 샘플링 → 이번 depth write 순서 보장, (2) depth write 완료 → main pass fragment 샘플링 순서 보장. 이 두 dependency가 없으면 GPU가 shadow map을 읽는 도중 덮어쓰는 race condition 발생.
- `VkFramebuffer` (shadow image view 연결).
- 소멸자에 5개 리소스 정리 추가.
- 이 단계에서 화면 변화 없음. 다음 단계(shadow pipeline + shadow pass 실행)의 기반.

### Shadow Pass Pipeline
- `shadow.vert` 신규 셰이더: push constant `mat4 lightMVP` 하나만 받아 위치 변환. UBO 불필요 — light matrix는 매 프레임 바뀌고 double-buffering이 필요 없어 push constant가 적합 (최소 보장 크기 128 bytes, mat4 = 64 bytes).
- `createShadowPipeline()`: fragment shader 없는 depth-only 파이프라인. `ChunkVertex` binding (stride 36 bytes) + location 0(pos)만 선언. `cullMode = FRONT_BIT` (back-face shadow map에서 peter-panning 억제), `depthBias` 상수 2.0 + slope 1.5 (shadow acne 방지). viewport/scissor 고정 1024×1024.
- `m_shadowPipelineLayout`: push constant range 1개(VERTEX stage, 64 bytes). descriptor set 없음.
- `drawFrame`에서 매 프레임 light matrix 계산: `elevation = sin(tod×π)`, `azimuth = tod×2π`로 sunDir 구성 → `glm::lookAt(player + sunDir×150, player, Z_UP)` + `glm::ortho(±60, ±60, 1, 300)` + Vulkan Y-flip.
- `recordCommandBuffer` 첫 부분에 shadow pass 삽입: shadow render pass begin → pipeline bind → push constant → 청크 메시 전체 draw → render pass end. 이후 기존 main pass 실행.
- 결과: 매 프레임 청크 메시의 깊이가 태양 시점으로 shadow map에 기록됨. 화면 변화 없음 — 다음 단계(descriptor에 shadow sampler 추가 + fragment shader에서 비교)에서 실제 그림자가 보임.

### Shadow Sampler + Descriptor 연결
- `createShadowSampler()`: `VK_COMPARE_OP_LESS_OR_EQUAL` comparison sampler. `CLAMP_TO_BORDER` + `FLOAT_OPAQUE_WHITE`(depth=1.0) — shadow map 밖 영역은 항상 lit 처리. `LINEAR` filter로 PCF 효과(비교 결과를 4 texel bilinear 평균).
- `createDescriptorSetLayout()`: binding 1 추가 — `COMBINED_IMAGE_SAMPLER`, `FRAGMENT_BIT`.
- `createDescriptorPool()`: `COMBINED_IMAGE_SAMPLER` pool size 추가 (`MAX_FRAMES_IN_FLIGHT`개).
- `createDescriptorSets()`: binding 1에 shadow image view(`DEPTH_STENCIL_READ_ONLY_OPTIMAL`) + sampler 바인딩. `vkUpdateDescriptorSets`에 write 2개로 UBO + sampler 동시 업데이트.
- 이 단계에서 화면 변화 없음. 다음 단계(chunk.vert에서 light space 좌표 출력 + chunk.frag에서 `sampler2DShadow`로 비교)에서 실제 그림자가 보임.

### Shadow Rendering
- UBO에 `mat4 lightMVP` 추가 (208 → 272 bytes). `updateUniformBuffer()`에서 `m_lightMVP`를 UBO에 기록.
- `chunk.vert` / `object.vert` / `triangle.vert`: UBO에 `lightMVP` 추가, `fragPosLightSpace = ubo.lightMVP * vec4(worldPos, 1.0)` 출력 (location 2 또는 3).
- `chunk.frag` / `triangle.frag`: `sampler2DShadow shadowMap` (binding 1). `fragPosLightSpace` perspective divide → UV 변환 (`xy * 0.5 + 0.5`) → NdotL 기반 bias(`mix(0.008, 0.001, NdotL)`) 적용 후 `texture(shadowMap, vec3(projCoords.xy, z - bias))` 비교. `shadowFactor = max(shadow, 0.4)` — 그림자 안도 40% 밝기 유지.
- 결과: 지형·나무·플레이어에 그림자가 드리워지고 시간에 따라 방향 변화.

### Shadow Quality Tuning + Depth Fix
- Shadow map 2048×2048 (기존 1024), ortho range ±80 (초기 ±60 → ±45 시도 후 최종 ±80). texel 크기 ≈ 0.078 units/texel.
- `GLM_FORCE_DEPTH_ZERO_TO_ONE` 를 `CMakeLists.txt`에 추가. 근본 원인: GLM `ortho`가 기본적으로 OpenGL 깊이 범위 [-1,1]로 계산 → Vulkan [0,1] 불일치 → 플레이어 근처 geometry가 NDC Z ≈ 0 경계에 걸려 shadow map에서 들어갔다 나갔다 → 이동할 때 그림자 잘림. 수정 후 정상 동작.
- **미결 항목 (추후 처리):**
  - Shadow aliasing (경계 계단 현상): PCF 샘플 수 증가로 개선 가능. 로우폴리 스타일에서 허용 범위 안이므로 렌더링 폴리시 2차 때 검토.

### 태양 방향 수정
- `azimuth = timeOfDay * 2π` → `-timeOfDay * 2π`. 한 줄 수정으로 그림자 회전 방향이 더 자연스러워짐.

### Fog (안개)
- UBO에 `vec4 fogColor` 추가 (272 → 288 bytes). `updateUniformBuffer()`에서 `m_skyColor`를 그대로 기록 → 안개 색이 시간대 sky color와 자동 동기화 (낮=파랑, 노을=주황 등).
- `chunk.vert` / `object.vert` / `triangle.vert`: view-space 깊이 계산 `fragViewDepth = -(ubo.view * worldPos).z` 추가 (각 location 3, 3, 4).
- `chunk.frag` / `triangle.frag`: `FOG_START=27, FOG_END=57` 선형 안개. `fogFactor = clamp((END - depth) / (END - START), 0, 1)` → `mix(fogColor, litColor, fogFactor)`. 안개 범위는 카메라 기준이므로 플레이어 기준 약 7~37 유닛 밖에서 적용.
- 결과: 먼 지형이 하늘색으로 자연스럽게 희미해지고 청크 경계가 가려짐.

### Shadow PCF (3×3)
- `chunk.frag` / `triangle.frag`: 단일 `texture()` 호출 → 3×3 루프로 교체. `texel = 1.0 / 2048.0`, 각 샘플에 같은 bias 적용 후 9로 나눔.
- `LINEAR` 샘플러가 이미 하드웨어 2×2 bilinear PCF를 수행하므로 수동 3×3과 결합하면 샘플링 부드러움 개선.
- 참고: 지형의 계단 모양 그림자는 shadow map 문제가 아니라 블록 지형 자체의 기하학적 계단에서 기인. PCF로 해결 불가, 로우폴리 복셀 스타일에서 자연스러운 특성으로 수용.

### 리사이즈 viewport 수정 (dynamic state)
- 파이프라인이 viewport/scissor를 정적 상태로 구워, 창 리사이즈 후에도 옛 크기로 렌더돼 화면이 한쪽으로 쏠리거나 잘리던 버그 수정.
- 메인 패스 파이프라인 4개(triangle/chunk/ui/object)에 `VK_DYNAMIC_STATE_VIEWPORT` + `VK_DYNAMIC_STATE_SCISSOR` 추가, `recordCommandBuffer`의 메인 패스 시작 직후 `vkCmdSetViewport`/`vkCmdSetScissor`를 `m_swapchainExtent` 기준으로 매 프레임 설정.
- shadow 파이프라인은 2048×2048 고정이라 정적 viewport 유지. `recreateSwapchain`은 파이프라인 재생성 없이도 새 크기에 맞게 렌더됨.

### 프레임별 동적 버퍼 분리 (frame-in-flight 경합 수정)
- player/selector/UI 인스턴스 버퍼가 단일 버퍼라 매 프레임 in-place `memcpy` → frame N의 GPU가 읽는 도중 frame N+1이 덮어쓰는 data race 존재.
- `m_playerInst*` / `m_selectorInst*` / `m_ui*`(buffer·memory·mapped)를 `std::vector`로 전환해 `MAX_FRAMES_IN_FLIGHT`개씩 생성, update/bind를 `[m_currentFrame]`로 분리 (UBO와 동일 패턴).
- selector 정점/인덱스 버퍼는 정적이라 단일 유지. `m_uiVertexCount`는 같은 프레임 내에서 쓰고 그리므로 단일 값 유지.

### present semaphore 이미지별 분리 + imagesInFlight
- present용 `m_renderFinished` semaphore가 frame-in-flight 단위(2개)라, present wait가 끝나기 전에 같은 semaphore가 재signal돼 검증 레이어가 잡던 동기화 위반 수정.
- `m_renderFinished`를 스왑체인 이미지 개수만큼 생성하고 submit signal / present wait를 `[imageIndex]`로 변경. imageAvailable·inFlight fence는 frame-in-flight 단위 유지.
- `m_imagesInFlight`(이미지별 fence 참조) 추가 — acquire한 이미지가 이전 프레임에서 아직 사용 중이면 그 fence를 먼저 대기. `recreateSwapchain`에서 이미지 수 변동에 대비해 semaphore·추적 배열 재생성.

### Frustum near 평면 수정 (Vulkan depth)
- `GLM_FORCE_DEPTH_ZERO_TO_ONE`(Vulkan [0,1] depth)를 쓰는데 Gribb-Hartmann near 평면을 OpenGL [-1,1] 공식(`row(3)+row(2)`)으로 추출하던 버그 수정 → `row(2)`로 변경.
- 나머지 5개 평면(left/right/bottom/top/far)은 clip-space x·y, far 모두 두 깊이 규약에서 동일하므로 그대로.

### 죽은 코드 제거 (updateTargetTile)
- 마우스 레이캐스팅으로 대체돼 호출처가 없던 `GameState::updateTargetTile` 선언/정의 제거. 동작 변화 없음.

### 태양 방향 계산 중복 제거
- `drawFrame`(lightMVP 계산)과 `updateUniformBuffer`(UBO `lightDir`)에서 중복 계산하던 `elevation`/`azimuth`/`sunDir`를 `drawFrame`에서 한 번만 계산해 `m_sunDir`/`m_dayFactor` 멤버에 저장.
- `updateUniformBuffer`는 멤버를 읽기만 하고, 미사용이 된 `timeOfDay` 파라미터 제거. 렌더 결과는 동일.

### 밤 shadow pass 스킵 (최적화)
- 프래그먼트 셰이더가 `dayFactor > 0.01`일 때만 그림자를 샘플링하므로, 밤(태양 지평선 아래)엔 shadow map 청크 렌더를 건너뜀 — 매 프레임 ~수십 개 청크를 태양 시점으로 다시 그리던 비용 절약.
- 렌더패스 begin/end + clear(depth=1.0=전부 lit)는 유지해 이미지 레이아웃 전환과 시작 첫 프레임(밤) 안전성 보장. 무거운 청크 draw 루프만 `m_dayFactor > 0.01f`로 가드.

### object 인스턴스 버퍼 재생성 회피 (최적화)
- 타일 변경·작물 성장 등으로 청크 메시가 리빌드될 때마다 나무 인스턴스 버퍼까지 매번 defer-destroy 후 재생성하던 낭비 제거.
- 나무는 지형 생성 시 한 번 배치된 뒤 불변이므로 `ChunkRenderData::objInstBuilt` 플래그로 청크 로드당 1회만 빌드. 언로드→재로드 시 새 `ChunkRenderData`(플래그 false)로 다시 빌드되어 정상.

### 청크 dirty 리빌드 프레임 분할 (최적화)
- `rebuildDirtyChunks`가 매 프레임 dirty 청크를 전부 빌드하던 것을 프레임당 `MAX_CHUNK_BUILDS_PER_FRAME`(=2)개로 제한. 초과분은 `dirty=true`로 남아 다음 프레임들에 분산.
- 청크 스트리밍(경계 넘을 때 한 줄 ~7개)·작물 성장(날짜 전환 시 다수 청크 동시 dirty) 시 한 프레임에 몰리던 스파이크 완화. 농장 게임 특성상 성장 색 변화는 지연에 둔감해 분산 처리가 적합. N은 튜닝 가능(우선순위/시간예산 방식은 추후).

### 입력 일원화 (InputManager)
- `main.cpp`이 직접 `glfwGetKey`로 읽던 ESC·Q/E·Ctrl+S를 `InputManager::pollInput`으로 이전. `PlayerInput`에 `quit`/`rotateLeft`/`rotateRight`/`saveKey` 추가.
- `Window::close()` 추가로 main이 raw glfw 호출 없이 종료 요청. main은 폴링된 `input`으로 종료/회전/저장(edge-detect) 처리. 동작 동일, 입력 수집이 한 곳으로 통일.

### 파이프라인 생성 보일러플레이트 헬퍼화
- 메인 패스 파이프라인 4개(player/selector·chunk·ui·object)가 복붙하던 shader stage·input assembly·viewport+dynamic·rasterizer·multisample·blend·depth·pipelineInfo 조립을 `createPipeline(PipelineConfig)` 헬퍼 하나로 통합.
- 각 함수는 vertex binding/attribute·cullMode·depthTest·alphaBlend·shader 경로·layout만 `PipelineConfig`로 채워 호출(~430줄 → ~110줄). pipeline layout 생성은 각 함수에 유지, shadow 파이프라인(depth-only·push constant·고정 viewport)은 성격이 달라 미변경. 렌더 결과 동일.

### 디스크 로드 청크 나무 복원 (버그 수정)
- `save.dat`에서 로드된 수정 청크는 오브젝트(나무)가 저장도 재생성도 되지 않아 나무가 사라지던 버그 수정.
- `World::load()`에서 청크마다 `TerrainGen::generate`를 임시 청크에 돌려 `objects`만 가져와 적용. 저장된 타일/상태는 보존하고 나무만 좌표 기반 결정론으로 원래대로 복원. 시작 시 수정 청크 수만큼 1회.

### shadow pass 라이트 프러스텀 컬링 (최적화)
- shadow pass(주간)가 로드된 청크를 전부 그리던 것을, `m_lightMVP`에서 추출한 라이트 프러스텀으로 청크 AABB 컬링. 태양 직교 박스 밖 청크는 어차피 shadow map에 안 잡히므로 그림자 손실 없이 draw call만 감소.
- 메인 패스 컬링과 동일한 `Frustum::extractFrom`/`containsAABB` 재사용. (4번 near 평면 수정 덕에 직교+[0,1] depth에도 정확)

### 나무 그림자 캐스팅
- shadow pass가 청크 메시만 그려 나무가 그림자를 못 드리우던 것 해결. `shadow_object.vert`(인스턴스 scale/rot/pos 변환 + push constant `lightMVP`) + `m_shadowObjectPipeline`(depth-only, `ChunkVertex`+`ObjectInstance`) 추가.
- shadow pass에서 청크 다음으로 나무 인스턴스를 그림 — 2a의 라이트 프러스텀 컬링 재사용, `m_shadowPipelineLayout`(push constant)·`m_shadowRenderPass` 공유. cullMode NONE(나무 메시 비watertight). 시간에 따라 그림자 방향 회전.

### 플레이어 그림자 캐스팅
- 플레이어 큐브도 shadow map에 기록. `shadow_player.vert`(pos + instancePos + push constant `lightMVP`) + `m_shadowPlayerPipeline`(depth-only, `Vertex`+`InstanceData`, cullMode FRONT_BIT=watertight 큐브 피터패닝 억제) 추가.
- shadow pass에서 나무 다음으로 플레이어 cube를 인덱스 draw. 라이트 박스 중심이라 컬링 없음.

### 문서 4종 체계 + 월드 모델 방향 결정
- README/DEVLOG에 더해 `ARCHITECTURE.md`(엔진 구조 + 기술 방향, `[구현됨]`/`[계획]` 태그)와 `DESIGN.md`(게임 기획·비전·스코프) 추가. README 상단에 4종 문서 역할·갱신주기 표.
- 갱신 정책: README=기능 추가 시 / DEVLOG=작업마다 / ARCHITECTURE=구조 변경 시 / DESIGN=방향 변경 시. 각 사실은 한 문서에만(중복·노후화 방지).
- 결정: 월드 모델은 **고정맵 + 절차 레이어** 방향(문서상). 청크=스트리밍 단위라 추후 `WorldSource` 추상화로 전환, 현재 구현은 절차생성 유지.

### 그림자 접지(contact) 튜닝
- 피터패닝(그림자가 geometry에서 떠 보임) 교정. 원인: 파이프라인 depthBias + 셰이더 bias 이중 적용 + front-face culling.
- 단계: ① 셰이더 bias `mix(0.008,0.001)` → `mix(0.0015,0.0003)` ② 파이프라인 depthBias `2.0/1.5` → chunk·player `0/0` ③ chunk·player shadow `cullMode FRONT → NONE`(접지 다이얼의 핵심). 나무는 `1.5/1.2`·NONE 유지(현 느낌 좋음).
- `max(shadow,0.4)`(파스텔 부드러움)는 유지. 잔여 극미세 틈은 shadow map texel 해상도 한계 — 필요 시 ortho 범위 축소/해상도↑로 개선.

### 태양 방위각 180° 스윕 + 방향 오프셋
- 그림자가 하루에 360° 돌던 것(부자연)을, 방위각을 `(timeOfDay-0.5)·π`로 바꿔 **동→남중→서 180°만** 스윕하도록 수정 — 자연스러운 일주 아크.
- `kSunAzimuth` 오프셋(225°)으로 기본 시점에서 그림자 방향을 원하는 각으로 정렬. **태양만 회전**(카메라 구도·이동 기준 불변). 태양 계산은 `drawFrame` 한 곳이라 조명·그림자에 동시 적용.

### 포스트 프로세스 패스 + 컬러 그레이딩
- 씬을 **오프스크린 컬러 타겟**(스왑체인 sRGB 포맷, frame-in-flight별 2개)에 렌더 → **풀스크린 post 패스**가 톤/색 보정 후 스왑체인에 출력. (UI는 현재 씬 패스라 함께 grading됨 — 추후 분리 가능)
- 구조: 기존 main 렌더패스를 **씬 패스**(color finalLayout=`SHADER_READ_ONLY`)로 전환, **post 렌더패스**(스왑체인) 신설, 정점버퍼 없는 풀스크린 삼각형 파이프라인(`post.vert`/`post.frag`) + 오프스크린 샘플 디스크립터. `recreateSwapchain`·소멸자에 오프스크린/디스크립터 재생성·해제 추가. 오프스크린=스왑체인 sRGB라 패스스루(0a)가 픽셀 동일.
- 그레이딩(`post.frag` 상수): exposure·contrast·saturation + split-tone(쿨 그림자/웜 하이라이트, 따뜻한 쪽으로 튜닝) + vignette. 이후 bloom 등 image-space 효과의 토대.

### 물주기 (watering) — 물이 성장을 gate
- `ItemType::TOOL_WATERINGCAN` + `TileState.watered`(일시 상태, 매일 리셋이라 미저장). 물뿌리개 우클릭 → FARMLAND `watered=true`, 짙은(촉촉) 색으로 렌더.
- `growthTick` 재작성: **물 준 farmland 위 WHEAT만 하루 1단계 성장**, 처리 후 farmland 마름(매일 재급수 필요). 기존 날짜기반 catch-up 제거.
- 인벤토리 그리드 `INV_ROWS 2→3`(아이템 전부 노출). 기본 핫바 도끼→물뿌리개로 교체(호미+물뿌리개+씨앗 = 농사 루프 즉시 사용, 도끼는 인벤토리에서).

### UI 숫자 렌더러 + Day HUD (1-B-i)
- 텍스처/폰트 없이 **3×5 도트matrix 숫자**(`pushNumber`)를 기존 UI 색 quad로 렌더. 좌상단에 현재 Day 표시.
- `drawFrame`에 `day` 파라미터 추가, UI 버퍼 512→1024 verts. 이 렌더러를 1-B-ii의 슬롯 개수 표시에 재사용. (인벤토리 키스톤의 선행 단계)

### 인벤토리 모델 — 스택+개수 (1-B-ii)
- `m_palette(ItemType[9])` → `ItemStack{type,count}[27]`(9×3, 핫바=앞 9칸). `GameState::inventory()` 노출, `drawFrame`로 전달.
- 핫바/인벤토리 창을 스택+개수로 렌더(`count>1`이면 숫자, 1-B-i 렌더러 공용). 인벤토리 창(`I`)은 27칸 **읽기전용** 그리드(드래그/재배치는 폴리시로 미룸).
- 시작 아이템: 호미·물뿌리개·씨앗×10·도끼. 씨앗은 심을 때 1 소모(0이면 빈칸), 도구는 무한. 무한 블록 팔레트·인벤토리 클릭-할당 폐지(지형 불변 방향과 일치).
- 미결: 좌클릭 지형 파괴는 아직 잔존(스타듀 아크 ④단계에서 제거 예정).

### 낫 수확 → 인벤토리 획득 (1단계 닫기)
- `ItemType`에 `TOOL_SICKLE`(낫)·`ITEM_WHEAT`(수확된 밀) 추가. `isTool()`에 낫 포함, `itemColor()` 케이스 추가. 시작 핫바 슬롯 4에 낫(키 5).
- `GameState::addItem(ItemType, count)` 신규 — 같은 타입 스택 우선 채우고 없으면 첫 빈 칸, 자리 없으면 false. **③ 자원 채집에서 그대로 재사용할 제네릭 API**.
- 좌클릭 분기 재구성: 대상이 `WHEAT`면 **낫 선택 + `growthStage==3`(완숙) + 인벤토리 여유**가 모두 맞을 때만 `AIR`로 만들고 `addItem(ITEM_WHEAT, 1)`. 미성숙/낫 아님/인벤토리 가득이면 작물 보호(변화 없음). WHEAT 아닌 타일은 기존 파괴 유지(④에서 제거 예정).
- 인벤토리는 save 대상이 아니라 호환성 영향 없음. 이로써 농사 루프(경작→심기→물주기→성장→수확→인벤토리)가 처음으로 완결.

### 작물 아래 흙 물주기 버그 수정
- 작물(WHEAT)이 흙(FARMLAND) 위 칸에 심기는데 타겟 선택이 "가장 위 non-AIR"라 작물이 항상 잡혀, 심은 뒤엔 흙에 재급수가 불가능 → 물-성장 게이트가 사실상 막히던 버그.
- 물뿌리개 분기: 타겟이 WHEAT면 한 칸 아래(`fz = tz-1`)를 보고 FARMLAND면 거기에 급수. 작물 없는 빈 흙은 기존대로 그 자리 급수.

### 드롭 아이템 + 줍기 (③ 채집 인프라 선행)
- `DroppedItem{pos, type, count}`(Types.h) + `GameState::m_drops` 벡터. 수확이 `addItem` 직행 → **바닥에 드롭 스폰**으로 변경(낫으로 완숙 밀 좌클릭 시 작은 큐브가 그 자리에 떨어짐).
- 줍기: `update()`에서 매 프레임 플레이어-드롭 **수평 거리**(반경 0.9) 검사 → 근접 시 `addItem` 성공하면 제거. 인벤토리 가득이면 바닥에 남아 대기.
- 렌더: `kVertices` 0.3배 작은 큐브(`createItemMesh`, DEVICE_LOCAL 1회) + 프레임별 드롭 인스턴스 버퍼(`InstanceData`, `itemColor` 색). **기존 플레이어 인스턴싱 파이프라인·인덱스 버퍼 재사용**, 플레이어 draw 직후 드롭 전체 1 draw call(`MAX_DROPS=256`). 이 드롭/줍기 레이어는 ③ 자원 채집(나무/돌)에서 그대로 재사용.
- 미결: 드롭은 save 대상 아님(바닥에 둔 채 재시작하면 소실). 필요 시 ③에서 저장 검토.

### 오브젝트 렌더링 일반화 (②a, 순수 리팩토링)
- 나무 전용 단일 메시(`m_treeVertex*`) → **타입 인덱싱 메시 레지스트리** `m_objectMeshes[ObjectType::COUNT]`(`ObjectMesh{vbuf,vmem,count}`). `createTreeMesh` → `createObjectMeshes`(타입별 `upload()` 헬퍼).
- 청크 단일 오브젝트 인스턴스 버퍼 → **타입별 그룹** `ChunkRenderData::objGroups`(`{type,buffer,memory,count}`). `buildChunkObjectBuffer`가 `chunk.objects`를 타입별로 묶어 그룹마다 버퍼 생성.
- main/shadow 오브젝트 draw 루프가 그룹을 순회하며 `m_objectMeshes[type]` 메시 + 그룹 버퍼 바인딩. 소멸자·`rebuildDirtyChunks` 정리도 그룹 단위.
- 타입은 여전히 TREE 하나뿐 — **화면·동작 변화 0**(회귀 없음 확인). ②b에서 ROCK + ObjectDef를 얹을 토대.

### ObjectDef 테이블 + ROCK 타입 (②b)
- `ObjectType::ROCK` 추가 + **데이터 주도 `ObjectDef` 테이블**(`objectDef(type)` lookup, Chunk.h): `castShadow/collidable/placeable/harvestTool/dropItem/dropCount`. TREE→도끼/WOOD, ROCK→곡괭이/STONE. `harvestTool/dropItem`은 ③ 채집에서, `placeable`은 ⑥ 건축에서 사용.
- `TOOL_PICKAXE` 아이템(enum+색) 추가, 시작 핫바 슬롯 6에 배치(③ 대비).
- ROCK 로우폴리 메시(눌린 8면체, 법선 중심 기준 바깥쪽 강제)를 `createObjectMeshes`에 추가 → 레지스트리가 실제로 2타입을 다룸을 검증.
- `TerrainGen::placeRocks` — 비숲(건조/개활, b<0.45) 평지에 sparse 배치. 나무(b>0.58)와 바이옴 분리라 겹치지 않음.
- shadow pass에 `objectDef(type).castShadow` 게이트 추가(현재 둘 다 true라 화면 변화 없음, ③/이후 타입 대비).
- `collidable=true`는 저장만, 이동 차단 미적용(나무·돌 통과). 채집 상호작용은 ③.

### 자원 채집 (③)
- `World::tryHarvestObject(x,y,tool,...)` — 타일 위 오브젝트를 찾아 `objectDef().harvestTool`과 도구가 맞으면 제거 + 드롭정보(`dropItem`×`dropCount`) 반환. 결과 `HarvestResult{NoObject, WrongTool, Harvested}`.
- 좌클릭 우선순위: **① 오브젝트 채집(도끼→나무/곡괭이→돌)** → ② 도구 안 맞으면 막힘(아래 지형 보호) → ③ 오브젝트 없으면 기존 작물 수확/타일 파괴. 채집물은 드롭/줍기 레이어로 바닥에 떨어져 줍기.
- `Chunk::objectsDirty` 플래그 신설 — 오브젝트 추가/제거 시에만 렌더러가 인스턴스 버퍼 재빌드(성장·타일변경 땐 스킵 유지). 기존 `ChunkRenderData::objInstBuilt` 대체.
- 채집 시 `chunk.modified=true`로 세션 내 언로드/재방문 시 유지. **한계: 디스크 save→재시작 시 오브젝트 결정론 재생성으로 respawn**(DESIGN 자원 재생 방향과 일치, 영구 채집은 추후 제거-기록 저장).
- 이로써 스타듀 오브젝트 경제 아크 ①②③ 완료(인벤토리/작물 경제 → 제네릭 오브젝트 → 채집).
- 버그 수정: 오브젝트 드롭이 바닥 블록에 파묻혀 안 보이던 문제. `objPos.z`(지면 윗면 0.5)에 −0.2를 적용해 큐브가 지면 아래(0.15~0.45)로 묻혔음 → `+0.2`로 변경해 지면 위(0.55~0.85)에 안착. (밀 드롭은 블록 중심 기준이라 −0.25가 맞았던 것 — 기준점 차이였음)

### 복셀 편집 은퇴 (④, 지형 불변)
- 좌클릭 타일 파괴(`setTile AIR`) 분기 제거 → 좌클릭은 **오브젝트 채집 + 작물 수확**만.
- 우클릭 복셀 블록 설치(`isBlock(item)`) 분기 제거 → 우클릭은 **호미/씨앗/물뿌리개**만.
- `BLOCK_*` 아이템 타입은 유지(채집 드롭 + ⑤ 제작 재료). 동작만 제거. `isBlock`/`itemToTile` 인라인 헬퍼는 미사용 상태지만 ⑤⑥ 대비 Types.h에 보존.
- DESIGN "지형 불변 / Minecraft식 복셀 설치·파괴 은퇴" 실현. 지형은 이제 authored·고정, 건축은 ⑥ 오브젝트 레이어 예정.

### 인벤토리 제작 (⑤a, 스타듀식 클릭형 레시피)
- `Recipe{result, resultCount, inputs[3], requiresWorkbench}` 테이블 + `craftingRecipes()`(Types.h, `ObjectDef`처럼 데이터 주도). 초기 레시피: 작업대 `WOOD×4`, 울타리 `WOOD×2`(둘 다 인벤 제작). 새 `ItemType` `ITEM_WORKBENCH`/`ITEM_FENCE`.
- `GameState`: `countItem`/`removeItem`/`craft(idx)` — 재료 확인 → 소모 → 결과 `addItem`, 인벤 가득이면 입력 롤백. 인벤 열렸을 때 제작 행 클릭(엣지 감지 `m_prevCraftClick`)으로 `craft`. `requiresWorkbench` 레시피는 인벤에선 미노출(⑤b 작업대용).
- UI: 인벤 창 아래 제작 패널(결과+입력 색 swatch+개수, 재료 부족 시 어둡게). 클릭 사각형은 공유 `craftRowRect`로 GameState와 동기화. UI 버퍼 1024→`UI_MAX_VERTS`(2048) + 오버플로 가드.
- 제작 메커니즘은 **클릭형 레시피 목록**(마크식 격자 배치 아님). 제작물은 인벤에 쌓이고 설치는 ⑥. 본격 UI 리뉴얼은 별도 UI 패스로 미룸.
- 마크식 2단계 계획: ⑤a 인벤 제작(기본) → ⑥ 설치(작업대를 바닥에) → ⑤b 작업대 근처에서 고급 레시피(`requiresWorkbench`) 해금.

### 오브젝트 설치/철거 (⑥a)
- `ObjectType` WORKBENCH/FENCE 추가 + `ObjectDef`(placeable=true, harvestTool=NONE=맨손철거, dropItem=자기아이템). `itemToObjectType()` 매핑. 메시: `pushBox` 헬퍼로 작업대(테이블 박스)·울타리(기둥2+가로대2).
- 설치(우클릭): placeable 아이템 → `World::placeObject(x,y,type)`(지면 위·물 아님·중복 금지, `hasObjectAt` 체크) → Object 추가 + 인벤 1 소모.
- 철거(좌클릭): `tryHarvestObject`에서 placeable 오브젝트는 **도구 무관 철거 + 아이템 회수**, 자연물은 기존대로 도구 필요.
- 버그 수정: `placeObject`가 `objectsDirty`만 켜고 `dirty`를 안 켜서 설치물이 안 보이던 문제 → `dirty`도 set(오브젝트 버퍼 재빌드는 `buildChunkBuffer`=dirty 게이트 안에서 일어남).

### 오브젝트 영속성 (⑥b, save v2)
- save 포맷 버전 1→2. 청크마다 `objects` 직렬화(type/pos/scale/rot). load는 TerrainGen 재생성 대신 저장된 objects를 직접 읽음.
- 효과 둘: ① 설치한 작업대/울타리 **재시작 후 유지**, ② 채집한 자연물(나무/돌) **respawn 버그 해소**(저장 objects가 채집/설치 결과를 반영). ③의 "디스크 재시작 시 respawn" 한계 제거.
- 기존 v1 세이브는 무시되고 새 월드(개발 중 합의된 호환 깨짐), 다음 저장에 v2로 갱신.
- 이로써 제작·건축 아크 중 ⑤a·⑥ 완료. 남은 건 ⑤b(작업대 근처 고급 레시피 해금).

### 작업대 근처 고급 레시피 해금 (⑤b)
- `World::isObjectTypeNear(x,y,type,radius)` — 플레이어 주변에 특정 오브젝트가 있는지 검색. `GameState`가 매 프레임 `m_nearWorkbench`(반경 2, WORKBENCH) 판정.
- `requiresWorkbench` 레시피는 작업대 근처일 때만 제작 패널에 노출 + 클릭 제작 허용. 판정값을 `drawFrame`으로 넘겨 렌더 필터(`m_nearWorkbenchHud`)와 클릭 필터가 같은 프레임 단일 값으로 동기화(행 위치 어긋남 없음).
- 데모 고급 레시피: 돌담 `STONE×2`(작업대 필요). `ObjectType::STONE_FENCE` + 메시(회색 벽) + ⑥ 설치/철거/save 인프라 그대로 재사용.
- **이로써 스타듀 오브젝트 경제 아크 ①~⑥ 전체 완성** — 인벤토리/작물 경제 → 제네릭 오브젝트 → 채집 → 지형 불변 → 제작(인벤+작업대 2단계) → 설치/철거+영속성.

### 중간점검 — 방향/마일스톤 재설정 (외부 분석 검토 후)
- 아크 ①~⑥ 완료 시점에 프로젝트 상태·기술부채·미래대비를 점검(외부 LLM 분석을 비판적으로 검토). 결론을 4개 문서에 반영.
- **핵심 판단**: 기술 기반은 이미 충분 — "기능 추가"보다 **iteration speed · 비주얼 정체성 · app-flow**가 ROI 높음. 비주얼은 **다시 만들기가 아니라 조율**(grading·fog·shadow·AO 이미 존재).
- **비주얼 노스스타 확정**(DESIGN): 스타일라이즈드 로우폴리 디오라마 룩(덕코프류 **그래픽만** 참고, 시스템·전투 연출은 차용 안 함).
- **기술 Tier 재정렬**(ARCHITECTURE): Tier 1(DevUI/프로파일링·`FrameRenderData` 스냅샷·`GpuBuffer` RAII·app-state/메뉴/설정) → Tier 2(height fog·hemisphere ambient·wind·variation·AA SMAA+FXAA·LUT) → Tier 3(오디오·IRenderPass·데이터화·테스트).
- **명시적 비목표 신설**(ARCHITECTURE): ECS rewrite·render graph·asset DB·material graph·job system·RTX/PBR/mesh shader/bindless — rule of 3/실제 병목 전엔 안 함(renderer addiction 방어선).
- **불변식 명문화**: 결정론 생성 / 레이어 분리(렌더러=스냅샷 소비) / 상태는 World·GameState 경유 / 지형 불변 / 세이브 버전 정책.
- **분석 정정 사항**(코드 실제 기준): 색 그레이딩·split-tone·fog는 이미 구현됨 / 청크 리빌드 throttle 존재 / 에셋·텍스처·애니메이션 거의 없음 → asset DB·material 시스템은 시기상조 / 좌표 변환은 이미 중앙화.
- **즉시 가능한 작은 완성도**로 식별: 오브젝트 충돌(`collidable` 이미 데이터, `canOccupy` 한 줄).

### 오브젝트 충돌 (즉시 완성도)
- `World::isCollidableAt(x,y)` — 타일 위 오브젝트 중 `objectDef.collidable`인 게 있으면 true. `hasObjectAt`(아무 오브젝트나)과 분리 — 후자는 설치 중복 방지용, 책임이 다름.
- `GameState::canOccupy`에 `&& !world.isCollidableAt(...)` 한 줄 추가. 이로써 데이터로만 있던 `collidable` 플래그가 실제 이동 차단으로 배선됨.
- 결과: 나무·돌·울타리·작업대·돌담을 통과하지 못함(건축이 장식→기능). 축 분리 이동이라 옆면 미끄러짐은 유지, 작물(WHEAT 타일)은 타일 충돌이라 영향 없음.

### FrameRenderData 스냅샷 (Tier 1-A, 순수 리팩토링)
- `drawFrame`의 인자 10개 → `FrameRenderData` 구조체 1개로 묶음(`VulkanContext.h`, 클래스 위). 기능마다 인자 +1 하던 압력 제거, 렌더러는 public 경계에서 스냅샷만 소비.
- by-ref/by-value를 기존 시그니처 그대로 미러링(camera/inventory/drops=참조, 나머지=값) → **동작·성능 불변**. 본문은 `frame.*` 기계적 치환, 호출처(`main.cpp`)는 중괄호 초기화.
- 내부 헬퍼(`updateUniformBuffer` 등)는 좁은 인자 유지 — 스냅샷을 더 깊이 배선하지 않음(수술적 변경, 스코프 크리프 방지). DevUI(Tier 1-C)가 읽고 쓸 접합면 확보.

### GpuBuffer RAII (Tier 1-B, 순수 리팩토링·전체 스윗)
- `VkBuffer`+`VkDeviceMemory` 수동 쌍 ~13종(스칼라/청크/오브젝트/프레임별 vector)을 move-only RAII 타입 `GpuBuffer`로 통합. `mapped`를 흡수해 평행 `m_*Mapped` vector 5개 제거. `createBuffer`는 out-param → **반환형**으로.
- **리스크 최소화 설계**: `operator VkBuffer()` 암시적 변환 + 멤버명 유지 → 바인딩/드로우 등 읽기 ~30곳 무변경 컴파일. move-only라 실수 복사는 컴파일 에러로 차단.
- **device 파괴 순서 함정 해결**: 소멸자 본문에서 `vkDestroyDevice` 전에 GpuBuffer 컨테이너를 명시적 `clear()`/`destroy()`(device 살아있을 때). 멤버 자동 소멸자는 이후 idempotent no-op. 수동 파괴 ~50줄 → ~15줄.
- 스테이징 버퍼는 로컬 `GpuBuffer`로 두어 scope-exit 자동 해제(수동 destroy 제거). deferred-deletion 큐는 `{GpuBuffer, frame}`로, flush는 erase-remove의 move/소멸이 해제를 담당.
- 동작·렌더링 불변. 청크 스트리밍/종료 시 validation 클린 확인.

### DevUI(ImGui) + GPU timestamp 프로파일링 (Tier 1-C)
- `PASTEL_DEV_BUILD` CMake 옵션 추가(기본 ON). 개발 빌드에서만 Dear ImGui `v1.89.9`를 FetchContent로 받아 GLFW/Vulkan backend 소스를 컴파일. 비개발 빌드에서는 의존성과 코드 경로가 빠짐.
- Vulkan 통합: ImGui 전용 descriptor pool 생성, command pool 생성 뒤 폰트 텍스처를 one-shot command buffer로 업로드. 종료 시 device가 살아있는 동안 ImGui backend/query/descriptor pool을 먼저 정리.
- 렌더 위치: 기존 `shadow pass → scene offscreen pass → post pass` 구조 유지. ImGui는 post fullscreen triangle 직후 같은 post render pass 안에서 스왑체인에 직접 렌더링 → 개발 UI는 color grading 영향을 받지 않아 읽기 좋음.
- 입력: F3로 Dev 패널 토글. `Window`가 기존 scroll callback을 유지하면서 ImGui GLFW callback도 전달하고, ImGui가 마우스/키보드를 캡처한 프레임에는 main에서 월드 클릭·핫바 스크롤·이동/단축키 입력을 차단.
- GPU timing: frame-in-flight별 timestamp query를 기록하고, 같은 슬롯 fence 대기 뒤 이전 결과만 읽어 GPU 강제 wait 없이 표시. 패널에 total/shadow/scene/post/imgui 구간 시간을 보여줌. timestamp 미지원 GPU는 `unavailable`로 표시하고 게임은 계속 실행.
- 검증 결과: CMake 재구성 후 ImGui FetchContent, 실행, F3 패널, 입력 캡처, GPU timing 표시 정상 확인.

### Pause 1차 App-state (Tier 1-D 슬라이스)
- `main.cpp`에 최소 `AppMode { Gameplay, Paused }` 추가. 전체 메인메뉴/로딩/설정까지 확장하지 않고, 현재 가장 거친 앱 흐름인 `ESC=즉시 종료`만 먼저 제거.
- `ESC`는 edge-detect로 Gameplay/Paused를 토글. DevUI가 키보드를 캡처 중일 때는 pause 토글이 새지 않도록 `applyDevUiInputCapture`에서 `quit` 입력도 차단.
- pause 중에는 카메라 Q/E 회전과 `GameState::update()`를 건너뜀. 결과적으로 시간 진행, 작물 성장, 월드 클릭, 인벤토리/핫바 입력, 드롭 줍기, 플레이어 이동이 멈추고 렌더는 마지막 상태를 계속 그림.
- 창 종료는 OS 창 닫기 버튼으로 유지. pause overlay, 메인메뉴, 설정, 명시적 quit 버튼은 다음 App-state 슬라이스로 보류.
- 검증 결과: `ESC` pause/resume, pause 중 입력 차단, DevUI 조작, 창 닫기 종료 정상 확인.

### 입력 컨텍스트 정리 (Tier 1-D 슬라이스)
- `main.cpp` 로컬 helper `clearGameplayInput` / `applyAppModeInputPolicy` 추가. pause 중 막아야 하는 이동·클릭·핫바·인벤토리·카메라 회전 입력 목록을 한 곳으로 모음.
- DevUI 키보드 캡처도 같은 `clearGameplayInput`을 재사용하도록 정리. 입력 차단 우선순위가 DevUI 캡처 → app mode 정책 → 게임 업데이트 순서로 읽히게 됨.
- 동작은 기존 pause와 동일: `ESC` pause/resume, F3 DevUI, Ctrl+S 저장 정책 유지. 추후 MainMenu/Settings 입력 컨텍스트를 추가할 접합면 확보.
- 검증 결과: pause 중 게임플레이 입력 차단, DevUI 입력 캡처, F3 토글 정상 확인.

### Pause 시각 피드백 (Tier 1-D 슬라이스)
- `FrameRenderData`에 `paused` bool 추가. 렌더러는 AppMode 자체가 아니라 프레임 스냅샷 값만 소비해 기존 레이어 분리를 유지.
- `main.cpp`가 `appMode == AppMode::Paused`를 `drawFrame`에 전달. 게임 업데이트 차단 로직은 기존 pause 슬라이스와 동일.
- 기존 UI quad 파이프라인으로 화면 전체 dim + 중앙 pause 아이콘(두 막대)을 렌더. 새 폰트/텍스처/의존성 없이 pause 상태를 시각적으로 확인 가능.
- 검증 결과: `ESC` pause/resume 시 overlay 표시/해제, DevUI 조작, 리사이즈 중앙 정렬 정상 확인.

### AppFlow 상태 묶기 (Tier 1-D 슬라이스, 순수 리팩토링)
- `main.cpp`의 app-flow 상태(`AppMode`, ESC/Ctrl+S/F3 edge-detect)를 로컬 `AppFlow` 구조체로 묶음. 전역 시스템이나 새 파일로 키우지 않고, 다음 app-state 확장을 위한 접합면만 정리.
- `updatePauseToggle`, `consumeSavePress`, `consumeDevUiToggle`, `gameplayActive`, `paused`로 main 루프의 상태 조회/edge-detect 의도를 명시.
- 동작 불변: `ESC` pause/resume, pause overlay, F3 DevUI 토글, Ctrl+S 저장, pause 중 입력 차단 유지.
- 검증 결과: pause/resume, DevUI 토글, 저장 edge-detect, pause overlay 정상 확인.

### Tiny UI Text 기반 + Pause 문구 (Tier 1-D 슬라이스)
- 기존 3×5 숫자 렌더러를 `0-9`/`A-Z` glyph 렌더러로 확장. 새 폰트·텍스처·의존성 없이 기존 UI quad 파이프라인만 재사용.
- `pushNumber`는 새 glyph 경로를 재사용하도록 바꾸어 Day HUD, 핫바/인벤토리 개수 표시 동작을 유지.
- pause overlay에 `PAUSED` 텍스트를 추가하고 중앙 pause 아이콘과 겹치지 않도록 위치를 조율. 메인메뉴/설정/로딩 화면에 필요한 최소 게임 UI 텍스트 기반 확보.
- 검증 결과: pause/resume 시 `PAUSED` 문구와 pause 아이콘 표시, 기존 숫자 UI 정상 확인.

### MainMenu 1차 App-state (Tier 1-D 슬라이스)
- `AppMode`에 `MainMenu`를 추가하고 앱 시작 상태를 메뉴로 변경. `Enter` edge-detect로 Gameplay에 진입하며, `ESC` pause/resume은 Gameplay/Paused 사이에서만 동작하도록 제한.
- `PlayerInput`에 `startKey`를 추가하고 `InputManager`에서 Enter를 읽음. MainMenu에서는 이동·카메라·마우스·인벤토리·핫바·Ctrl+S 저장 입력을 차단해 게임 시간이 시작 전 흘러가지 않도록 유지.
- `FrameRenderData`에 `mainMenu` bool을 추가. 렌더러는 AppMode를 직접 알지 않고 스냅샷 값만 소비해 기존 레이어 분리를 유지.
- Tiny UI Text로 `PASTEL FARM` / `PRESS ENTER` 메뉴 화면을 표시. 새 폰트·텍스처·의존성 없이 기존 UI quad 파이프라인 재사용.
- 검증 결과: 실행 직후 메뉴 표시, Enter 게임 진입, 메뉴 중 입력 차단, 진입 후 pause/resume 및 DevUI 정상 확인.

### 월드 세션 시작 지연 (Tier 1-D 슬라이스)
- 앱 시작 시 즉시 실행하던 `save.dat` 로드와 초기 청크 로드를 `startWorldSession` 람다로 이동. MainMenu는 더 이상 이미 준비된 게임 위에 덮이는 화면이 아니라, Gameplay 진입 전 대기 상태가 됨.
- `Enter` 입력을 consume할 때 save 로드 → 플레이어 위치/시간 복원 → 해당 위치 주변 청크 로드 → Gameplay 진입 순서로 실행. 시작 입력 프레임의 게임플레이 입력은 `clearGameplayInput`으로 비워 첫 프레임 누수 방지.
- `worldSessionStarted` 플래그로 메뉴 중 Ctrl+S 저장과 청크 스트리밍을 차단. 메뉴에서 기다려도 게임 시간·작물 성장·월드 로드가 진행되지 않음.
- 검증 결과: 메뉴 대기 중 시간 정지, Enter 후 save 위치/시간 복원, 월드 표시, 저장·pause/resume·DevUI 정상 확인.

### Loading 1차 App-state (Tier 1-D 슬라이스)
- `AppMode::Loading` 추가. MainMenu에서 `Enter`를 누르면 바로 Gameplay로 가지 않고 Loading 상태로 먼저 전환.
- `pendingWorldStart` 플래그로 `LOADING` 화면을 한 프레임 렌더한 뒤 `drawFrame` 이후 `startWorldSession()`을 실행. 현재 로드는 동기 방식이지만, 이후 비동기 로딩/세이브 선택/진행률 표시로 확장할 접합면 확보.
- `FrameRenderData`에 `loading` bool 추가. 렌더러는 AppMode를 직접 알지 않고 스냅샷 값만 소비해 `LOADING` Tiny UI Text 화면을 표시.
- loading 중에는 기존 app mode 입력 정책으로 게임플레이 입력과 저장이 차단됨. save 로드 + 초기 청크 로드가 끝나면 Gameplay로 진입.
- 검증 결과: MainMenu → `LOADING` 표시 → save/월드 로드 → Gameplay 진입, 진입 후 저장·pause/resume·DevUI 정상 확인.

### Settings 1차 App-state (Tier 1-D 슬라이스)
- `AppMode::Settings` 추가. MainMenu에서 `S` edge-detect로 Settings 화면에 진입하고, Settings에서 `ESC` edge-detect로 MainMenu에 복귀.
- `PlayerInput`에 `settingsKey`를 추가하고 `InputManager`에서 S 키를 읽음. AppFlow에서 MainMenu 상태일 때만 settings 진입에 사용해 Gameplay의 S 이동 입력과 분리.
- `FrameRenderData`에 `settings` bool 추가. Tiny UI Text로 MainMenu 보조 문구 `S SETTINGS`, Settings 화면 `SETTINGS` / `PRESS ESC`를 표시.
- Settings 중에는 기존 app mode 입력 정책으로 게임플레이 입력과 저장이 차단됨. 실제 설정값(해상도/vsync/AA 등) 변경은 다음 슬라이스로 보류.
- 검증 결과: MainMenu → Settings 진입, Settings → MainMenu 복귀, Settings 중 Enter 무시, Gameplay 진입 후 S 이동·저장·pause/resume·DevUI 정상 확인.

### Settings VSync 데이터 토글 (Tier 1-D 슬라이스)
- `main.cpp`에 로컬 `AppSettings` 구조체 추가. 현재는 `vsync` bool과 V 키 edge-detect만 보관하며, 전역 설정 파일/새 시스템으로 키우지 않음.
- `PlayerInput`에 `toggleVsyncKey`를 추가하고 `InputManager`에서 V 키를 읽음. `AppSettings::update`는 `AppMode::Settings`에서만 V 입력을 소비해 Gameplay 입력과 분리.
- `FrameRenderData`에 `vsyncEnabled` bool 추가. Settings 화면에 `VSYNC ON` / `VSYNC OFF`, `V TOGGLE`, `PRESS ESC`를 표시.
- 실제 Vulkan swapchain present mode 재생성/적용은 의도적으로 보류. 이번 슬라이스는 설정 데이터 모델 + UI 표시 + 입력 토글까지만 완료.
- 검증 결과: Settings에서 VSync ON/OFF 토글, Settings 재진입 시 값 유지, MainMenu/Loading/Gameplay/Pause 흐름 정상 확인.

### VSync present mode 적용 (Tier 1-D 슬라이스)
- 렌더러에 `m_vsyncEnabled` 상태 추가. `FrameRenderData::vsyncEnabled`와 값이 달라지면 프레임 초반에 swapchain을 재생성.
- `createSwapchain` present mode 선택을 설정값에 연결. VSync ON이면 `VK_PRESENT_MODE_FIFO_KHR`, OFF이면 기존처럼 `MAILBOX` 우선 + `FIFO` fallback.
- DevUI 프레임이 시작된 상태에서 재생성할 수 있어, 기존 out-of-date 처리와 동일하게 `ImGui::EndFrame()` 후 swapchain을 재생성하도록 처리.
- 검증 결과: Settings에서 VSync ON/OFF 토글 시 swapchain 재생성, 표시 유지, 리사이즈·MainMenu/Loading/Gameplay/Pause/DevUI 흐름 정상 확인.

### Settings 클릭형 row UI 전환 + AA 데이터 토글 (Tier 1-D 슬라이스)
- Settings 내부의 V/A 키보드 토글을 제거하고, 실제 게임 설정창에 가까운 클릭형 row UI로 전환. row는 `VSYNC ON/OFF`, `AA OFF/FXAA/SMAA`, `BACK`.
- `settingsRowRect`를 `Types.h`에 추가해 렌더링과 클릭 판정이 같은 screen-space rect를 공유하도록 정리. 기존 제작 UI의 `craftRowRect`와 같은 패턴.
- `AppSettings`는 Settings 상태에서 마우스 좌클릭 edge-detect로 row를 판정. VSync row는 설정값 토글 + 기존 present mode 적용 경로 사용, AA row는 `OFF → FXAA → SMAA` 데이터 순환, BACK row는 MainMenu 복귀.
- AA는 아직 실제 렌더링에 적용하지 않고 설정 데이터/UI만 제공. 이전 임시 `TOGGLE` 안내 문구를 제거해 AA 모드 변경 시 글자가 겹쳐 깨지던 문제도 함께 해소.
- 검증 결과: Settings row 클릭, VSync 적용, AA 데이터 순환, BACK/ESC 복귀, Gameplay의 A 이동 입력 정상 확인.

### MainMenu 클릭형 row UI 전환 (Tier 1-D 슬라이스)
- MainMenu의 `PRESS ENTER` / `S SETTINGS` 안내 문구를 제거하고, 클릭 가능한 `START` / `SETTINGS` row UI로 전환.
- `mainMenuRowRect`를 `Types.h`에 추가해 렌더링과 클릭 판정이 같은 screen-space rect를 공유하도록 정리. Settings row와 같은 폭/높이 규칙을 재사용.
- `AppFlow::consumeMainMenuClick`에서 마우스 좌클릭 edge-detect로 `START`와 `SETTINGS`를 판정. `START`는 기존 Loading → world session 시작 경로를 사용하고, `SETTINGS`는 Settings AppMode로 진입.
- `Enter` / `S`는 백업 입력으로 유지하되, 화면 노출은 클릭형 메뉴 중심으로 정리. Settings 진입 클릭 프레임이 Settings row 클릭으로도 소비되지 않도록 업데이트 순서를 분리.
- 검증 결과: MainMenu `START` 클릭, `SETTINGS` 클릭, Settings row 조작, BACK/ESC 복귀, Loading/Gameplay/Pause 흐름 정상 확인.

### Pause 클릭형 row 메뉴 + 인벤토리 ESC 우선순위 (Tier 1-D 슬라이스)
- Pause overlay를 단순 pause 아이콘에서 클릭형 `RESUME` / `SETTINGS` / `QUIT` row 메뉴로 전환. `pauseMenuRowRect`를 `Types.h`에 추가해 렌더링과 클릭 판정이 같은 screen-space rect를 공유.
- `AppFlow::consumePauseClick`에서 마우스 좌클릭 edge-detect로 pause 메뉴 row를 판정. `RESUME`은 Gameplay 복귀, `SETTINGS`는 Settings 진입, `QUIT`은 `Window::close()`로 창 종료 요청.
- Settings 진입 위치를 `settingsReturnMode`로 보관해 MainMenu에서 들어온 Settings는 MainMenu로, Pause에서 들어온 Settings는 Pause로 `BACK`/`ESC` 복귀.
- `RESUME` 클릭 프레임의 마우스 입력이 월드 클릭으로 새지 않도록 gameplay 입력을 비움. Settings 진입 클릭 프레임도 Settings row 클릭으로 이어지지 않도록 click edge 상태를 동기화.
- 인벤토리가 열린 Gameplay에서 `ESC`를 누르면 Pause 진입보다 인벤토리 닫기를 우선하도록 `GameState::closeInventory`와 `AppFlow::consumeInventoryEscape`를 추가. 인벤토리 UI와 Pause 메뉴가 동시에 겹쳐 보이던 문제 해결.
- 검증 결과: Pause `RESUME`/`SETTINGS`/`QUIT`, Pause → Settings → Pause 복귀, MainMenu → Settings → MainMenu 복귀, 인벤토리 열린 상태의 `ESC` 닫기 우선순위 정상 확인.

### 카메라 follow 댐핑 (Tier 2 비주얼/감각 튜닝)
- `Camera`가 플레이어 위치를 즉시 타겟으로 쓰지 않고 내부 `m_followTarget`을 지수 보간으로 따라가도록 변경. `main.cpp`는 `dt`를 넘겨 프레임레이트에 덜 의존하는 추적감을 사용.
- `snapToTarget`을 추가해 첫 업데이트와 Loading 후 월드 세션 시작 시에는 저장 위치로 즉시 스냅. 메뉴 → Gameplay 진입 때 먼 위치에서 길게 미끄러지는 상황 방지.
- 회전(`Q/E`)은 기존처럼 즉시 반응하고, follow 댐핑은 타겟 위치에만 적용. 마우스 피킹과 이동 방향 계산은 기존 `Camera` 행렬을 그대로 사용.
- 검증 결과: 이동 시 카메라 추적감, 메뉴→Loading→Gameplay 진입 스냅, Pause/Settings 중 drift 없음, Q/E 회전 및 월드 클릭 정상 확인.

### Hemisphere/colored ambient + 밤 밝기 조율 (Tier 2 비주얼 튜닝)
- `chunk.frag`와 `triangle.frag`의 조명 계산에서 `fragNormal`을 한 번 normalize해 shadow bias, diffuse, ambient 방향 계산에 공유.
- 기존 scalar ambient(`mix(0.15, 0.30, dayFactor)`)를 법선 방향 기반 hemisphere ambient로 변경. 윗면은 `SKY_AMBIENT`(cool), 아래/측면은 `GROUND_AMBIENT`(warm)를 섞어 로우폴리 면 방향이 더 읽히게 조율.
- diffuse·shadow·fog 구조는 유지하고 ambient tint만 곱함. C++ UBO나 파이프라인 구조 변경 없이 셰이더 상수 튜닝으로 제한.
- 검증 후 밤이 조금 밝다는 피드백에 따라 ambient 바닥값을 `0.15 → 0.10`으로 낮춤. 낮 최대값 `0.30`은 유지해 낮 장면 변화는 최소화.
- 검증 결과: 셰이더 컴파일/실행 정상, 낮 색감 유지, 밤 장면 더 어둡게 조율, 플레이어/셀렉터 색 이상 없음.

### 메뉴 상태에서 동적 월드 엔티티 렌더 스킵 (정리)
- MainMenu/Settings/Loading에서도 `recordCommandBuffer`가 플레이어 큐브를 항상 그려, 메뉴 딤 뒤로 주황 큐브가 비치던 문제를 정리.
- 렌더러가 이미 보유한 HUD 플래그로 `worldVisible = !(mainMenu || settings || loading)`를 계산해 셀렉터·플레이어·드롭 드로우를 게이트. Pause는 게임 위 오버레이라 의도적으로 포함(월드 표시 유지).
- `FrameRenderData`/`main.cpp` 호출부 변경 없이 `recordCommandBuffer` 한 곳만 수정. 청크/오브젝트/잔디 루프·UI·post·shadow 패스는 불변.
- 셀렉터·드롭은 현재 메뉴에서 비어 있지만 함께 게이트해, 이후 Pause→타이틀 복귀 구현 시 직전 플레이 상태가 타이틀 뒤로 새지 않도록 대비.
- 검증 결과: MainMenu에서 플레이어 큐브 비표시, Gameplay 플레이어/셀렉터/드롭 정상, Pause에서 월드 유지 정상 확인.

### hemisphere ambient 웜톤 조정 (Tier 2 비주얼 튜닝)
- 기존 `SKY_AMBIENT`가 파랑 우세(`0.74, 0.84, 1.08`)라 윗면이 차갑게 물들던 것을 `chunk.frag`/`triangle.frag` 양쪽에서 따뜻한 중립(`0.96, 0.93, 0.88`)으로 변경. `GROUND_AMBIENT`는 약간 더 따뜻하게(`1.04, 0.90, 0.70`).
- 결과적으로 hemisphere 차이가 "cool vs warm"이 아니라 "따뜻함의 정도"로 읽혀 DESIGN의 따뜻한 톤 방향에 정렬. sky의 밝기(평균 luminance)는 이전과 비슷하게 유지해 낮 장면 노출 변화를 최소화.
- 밤 ambient 바닥값(`0.10`)·diffuse·shadow·fog 구조는 불변. 셰이더 상수만 수정.
- 검증 결과: 낮 색감이 덜 푸르게 바뀜 확인. 단 낮 ambient 절대 변화량이 작아 체감은 미세 — 추후 LUT/grading 단계에서 더 또렷해질 여지.

### 마우스 좌표 프레임버퍼 픽셀 정합 (HiDPI 클릭/피킹 정렬)
- 기존엔 `input.mouseX/Y`가 `glfwGetCursorPos`(창/스크린 좌표)인데 `windowWidth/Height`는 `glfwGetFramebufferSize`(픽셀)라, 디스플레이 배율이 1이 아닐 때 클릭 판정·월드 피킹이 보이는 위치와 어긋날 수 있었음.
- `InputManager::pollInput`에서 커서를 `cursorPos × (framebufferSize / windowSize)`로 스케일해 프레임버퍼 픽셀로 변환하고, `windowWidth/Height`도 같은 자리에서 프레임버퍼 기준으로 설정. 마우스·창크기·렌더(스왑체인)가 모두 동일 픽셀 공간.
- 모든 소비처(메뉴/Pause/Settings row 클릭, 핫바·인벤·크래프팅 클릭, 월드 타일 피킹)가 같은 `PlayerInput` 필드를 읽으므로 단일 지점 수정으로 일괄 정합.
- 배율 1 디스플레이에선 스케일 계수가 1이라 동작 불변(회귀 없음). DevUI(ImGui)는 자체 입력 처리라 무관.
- 검증 결과: 일반 모니터에서 메뉴/핫바/인벤/크래프팅 클릭·월드 피킹 기존과 동일, 리사이즈 후 정합 유지 확인.

### `S` 키 입력 충돌 정리 + Ctrl+S 후진 버그 수정
- `S`가 후진(`moveBackward`) · 메뉴 SETTINGS 백업(`settingsKey`) · 저장(`Ctrl+S`)에 3중으로 쓰여 취약했고, 특히 게임 중 `Ctrl+S` 저장 시 `S`가 같이 눌려 플레이어가 뒤로 한 발 움직이는 실제 버그가 있었음.
- 메뉴 클릭 UI가 완성됐으므로 키보드 백업(`Enter`=START, `S`=SETTINGS)을 제거해 메뉴를 **클릭 전용**으로 단순화. `PlayerInput::startKey`/`settingsKey`, `InputManager`의 해당 읽기, `AppFlow::consumeMainMenuStart`/`updateMainMenuSettings` + 관련 `prev*` 상태와 DevUI 캡처 클리어까지 함께 제거.
- `InputManager`에 `ctrlHeld` 지역값을 두어 `moveBackward = S && !ctrlHeld`로 게이트하고, `saveKey = ctrlHeld && S`로 재사용. 이제 `S`는 후진/저장에만 쓰이고 둘은 Ctrl 유무로 명확히 분리.
- 검증 결과: MainMenu에서 `Enter`/`S` 무반응·클릭만 동작, gameplay `S` 후진 정상, `Ctrl+S` 저장 시 후진 없음·저장 정상, Settings/Pause 흐름 불변 확인.

### Pause QUIT을 앱 종료에서 타이틀 복귀로 변경
- 기존 Pause `QUIT`은 `Window::close()`로 앱을 완전히 종료했으나, 타이틀(MainMenu)로 복귀하도록 변경.
- `World::reset()` 추가 — `m_chunks`와 `m_modifiedUnloaded`를 모두 비움. `load()`가 맵을 비우지 않고 추가만 하므로, 이를 안 비우면 이전 세션의 미저장 편집이 다음 세션에 샐 수 있어 둘 다 clear.
- `main.cpp`에 `endWorldSession` 람다 추가: `world.reset()` + `gameState = GameState{}`(인벤토리/드롭/시간 fresh) + 세션 플래그 false. 다음 프레임 `rebuildDirtyChunks`가 청크 GPU 버퍼를 해제해 타이틀 뒤 잔상 제거. `AppFlow::returnToTitle()`로 Paused→MainMenu 전이.
- 재시작(START)은 깨끗해진 맵에 `startWorldSession`이 save를 다시 로드 → 첫 실행과 동일한 fresh 진입. 미저장 변경은 버려짐(의도). 인앱 완전 종료는 창 X 버튼만 남음(`Window::close()`는 호출처 없이 보존 — 추후 타이틀 EXIT/저장확인에 재사용 후보).
- 검증 결과: Pause QUIT→타이틀 복귀(앱 유지), 청크/플레이어/드롭 잔상 없음, START 재시작 시 save 정상 로드·세션 잔여 없음, 클릭 한 번에 하나만 동작 확인.

### Grid 규칙 vs Organic 표현 방향 정리 (Tier 2 비주얼 원칙)
- terrain breakup을 타일별 deterministic vertex color tint로 시도했으나, 잔디 타일마다 색이 바뀌어 grid가 더 강하게 드러나는 문제가 확인됨. 변경은 즉시 되돌림.
- 결정: **게임 규칙은 grid, 시각 경험은 organic**. 농사, 오브젝트 설치/철거, 충돌, 저장 좌표는 grid 기반을 유지하되 자연 바닥과 숲/풀/흙 표현은 100% grid처럼 보이면 안 됨.
- 타일별 색 랜덤/강한 per-tile hue variation은 금지 방향. 이는 Minecraft식 블록 월드 느낌을 강화함.
- 다음 breakup 방향은 비격자 dressing layer: 낮은 풀 clump, 잔돌, 흙/마른 풀 패치, 길 가장자리, 덤불/꽃/forage. 좌표 기반 결정론은 유지하되 저장 대상이 아닌 재생 가능한 시각 레이어로 시작.

### Vegetation alpha card 투자 판단 (Tier 2 비주얼 방향)
- 현재 기하 기반 풀 clump는 풀밭의 방향성 검증에는 유용하지만, 얇은 삼각형 실루엣 때문에 멀리서 삐쭉한 바늘처럼 보이는 한계가 확인됨.
- 참고 이미지에 가까운 풀은 alpha texture card가 더 적합. 목표는 낮고 풍성한 X자/부채꼴 card clump, 색/높이/회전 variation, 밀도 rule, 약한 wind sway.
- 현재 OceanVoyage 기준은 RTX 3060급으로 상향됐다. grass 같은 부가 요소도 저사양 회피보다 안정적인 품질 기법과 GPU timing 기반 검증을 우선한다.
- 새 텍스처와 alpha 파이프라인이 들어가므로 구현 전 설계안 필요. 텍스처 0개 원칙의 첫 예외가 될 수 있으나 vegetation은 ROI가 높은 예외로 판단.

### 절차 grass alpha 텍스처 리소스 추가 (Vegetation alpha card Step 1)
- alpha card 방식으로 전환하기 위한 첫 단계로, 런타임에서 작은 RGBA grass alpha 텍스처를 절차 생성하도록 추가.
- `createGrassTexture()` 안에 픽셀 채움 로직을 격리해, 이후 이미지 파일 기반 텍스처로 교체하더라도 파이프라인·디스크립터·메시 경로는 그대로 유지할 수 있게 함.
- staging buffer에서 device-local `VkImage`로 업로드하고, `UNDEFINED → TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL` 레이아웃 전환 후 image view와 sampler를 생성.
- 이미지 전송용 `transitionImageLayout` / `copyBufferToImage` 헬퍼를 추가. 기존 `copyBuffer`와 one-shot command buffer 보일러플레이트가 겹치지만, 이번 기능 커밋에서는 추출 리팩토링을 섞지 않음.
- 아직 grass 렌더 경로와 셰이더에는 연결하지 않았으므로 시각 변화 없음이 정상. 유저 빌드 검증 결과: 컴파일·실행·종료 정상, Vulkan validation 에러 없음.

### Grass alpha card 메시와 셰이더 준비 (Vegetation alpha card Step 2)
- alpha card 렌더링을 위한 전용 `GrassCardVertex`(`pos`, `normal`, `uv`)를 추가. 기존 `ChunkVertex`에는 UV가 없으므로 지형/오브젝트 정점 포맷을 건드리지 않고 grass 전용 포맷으로 분리.
- `m_grassCardMesh`에 X자로 교차한 quad 2장(총 4 triangles)을 생성. 기존 기하 grass clump 메시와 렌더 경로는 아직 유지해 시각 결과를 바꾸지 않음.
- `grass.vert` / `grass.frag` 추가. 인스턴스 위치·스케일·회전을 적용하고, grass 텍스처 alpha를 기준으로 `discard`하는 alpha test 셰이더를 준비.
- grass fragment shader는 기존 `chunk.frag`의 ambient·diffuse·shadow·fog 흐름을 최대한 맞춰 이후 렌더 경로 연결 시 조명 톤이 크게 튀지 않도록 함.
- CMake 셰이더 컴파일/복사 목록에 grass 셰이더를 추가. 유저 빌드 검증 결과: 셰이더 컴파일·앱 실행 정상, 기존 grass clump 시각 유지.

### Grass alpha card 파이프라인과 디스크립터 준비 (Vegetation alpha card Step 3)
- grass 전용 `m_grassPipeline`을 추가. 정점 입력은 `GrassCardVertex + ObjectInstance`, 셰이더는 `grass.vert` / `grass.frag`, 양면 카드 렌더링을 위해 cull mode는 `NONE`으로 설정.
- alpha는 블렌딩이 아니라 fragment shader의 alpha test(`discard`)로 처리하므로 `alphaBlend=false`, depth test/write는 기존 월드 오브젝트처럼 켜둠.
- 기존 scene descriptor layout을 `UBO(binding 0) + shadow map(binding 1) + grass texture(binding 2)`로 확장. 기존 청크/오브젝트/플레이어 셰이더는 binding 2를 사용하지 않으므로 같은 descriptor set을 계속 공유.
- descriptor pool과 descriptor set update에 grass texture sampler write를 추가. 텍스처는 프레임별로 변하지 않지만, 기존 프레임별 scene descriptor set 구조에 맞춰 각 set에 같은 grass texture를 기록.
- 아직 렌더 경로는 기존 기하 grass clump를 유지하므로 시각 변화 없음이 정상. 유저 빌드 검증 결과: 컴파일·실행·종료 정상, 기존 grass clump 시각 유지, Vulkan validation 에러 없음.

### Grass alpha card 렌더 경로 연결 (Vegetation alpha card Step 4)
- 메인 scene pass에서 기존 기하 grass clump draw를 `m_grassCardMesh + m_grassPipeline` draw로 교체해 alpha card grass가 실제 화면에 나오도록 연결.
- 기존 per-chunk grass instance buffer(`data.grassBuffer`, `grassCount`)는 그대로 재사용. 배치 좌표, 밀도, 스케일, 회전은 좌표 해시 기반 결정론을 유지.
- grass draw를 object draw와 분리해 먼저 `m_grassPipeline`으로 카드 식생을 그리고, 이후 나무·돌·작업대·울타리는 기존 `m_objectPipeline`으로 계속 렌더링.
- shadow pass에는 grass를 추가하지 않음. 식생은 시각 dressing layer로 유지하고, shadow caster는 기존 청크·오브젝트·플레이어 범위 그대로 둠.
- 유저 빌드 검증 결과: 컴파일·실행 정상, 기존 바늘형 clump보다 개선된 alpha card grass 표시 확인. 레퍼런스 수준의 풍성한 잔디까지는 밀도·색·형태·배치 rule 튜닝이 남았으며 Step 5에서 다룸.

### Grass dressing 재생성 게이트와 1차 튜닝 (Vegetation alpha card Step 5)
- `Chunk::grassDirty` 플래그를 추가해 grass instance buffer 재생성을 terrain/open-sky/object 변화가 있을 때로 제한. `setTile`, 오브젝트 설치/수확은 grass 배치에 영향을 주므로 `grassDirty=true`로 표시.
- `setTileState`, 작물 성장, 물주기처럼 grass 배치와 무관한 dirty 변경에서는 grass buffer를 재생성하지 않도록 분리. 작물/농지 visual 리빌드는 유지하되 식생 dressing 낭비를 줄임.
- grass 밀도를 18% → 28%로 올리고, 위치 오프셋과 scale 범위를 약간 키워 듬성한 느낌을 완화.
- alpha card mesh는 조금 더 낮고 넓게 조정하고, 절차 grass texture의 blade 수·폭·색을 늘려 기존보다 두꺼운 clump로 보이게 함.
- 검증 결과: 기존보다 두꺼워지고 alpha card 적용은 안정적이나, 아직 레퍼런스처럼 바닥을 덮는 실제 잔디밭 느낌은 부족. 다음 개선은 단순 밀도 증가보다 density field, variant, ground dressing layer가 핵심.

### SaschaWillems/Vulkan 레퍼런스 운용 문서 추가
- 교수님 추천 레포인 `SaschaWillems/Vulkan`을 Pastel Farm의 Vulkan 참고 기준으로 채택하되, 엔진 구조를 그대로 이식하지 않고 기능별 패턴 사전으로 쓰기로 정리.
- `VULKAN_REFERENCES.md`를 추가해 texture upload, instancing, alpha-to-coverage, shadow, indirect draw, debug utils, pipeline statistics 등 우리 엔진에 유용한 샘플과 적용 판단을 분류.
- `ARCHITECTURE.md`에는 외부 Vulkan 레퍼런스 운용 기준이 새 문서에 있다는 연결만 추가하고, `README.md` 문서 목록에 새 문서를 등록.
- Step 6 grass dressing에는 우선 `buildGrassDressingBuffer`의 좌표 기반 density field와 scale/rotation/offset variation을 적용하고, 인스턴스 포맷/texture array/alpha-to-coverage는 실제 필요가 확인될 때 확장하는 방향으로 정리.

### 문서 최신화 패스
- `README.md`의 긴 시간순 구현 목록을 현재 기능 스냅샷으로 압축하고, 세부 구현 이력은 `DEVLOG.md`로 분리해 문서 역할을 명확히 함.
- `ARCHITECTURE.md`의 렌더러/월드 요약을 현재 코드에 맞춰 grass pipeline, post pass, 제네릭 오브젝트, save v2 오브젝트 직렬화 기준으로 갱신.
- `DESIGN.md`의 현재 구현 상태와 grass alpha card 문구를 최신화.
- `DEVLOG.md` 하단의 오래된 Minecraft식 설계 메모를 현재 Stardew-style 지형 불변/StaticProp 방향 요약으로 교체. 과거 구현 기록은 시간순 이력으로 보존.

### Grass density field 기반 dressing 1차 (Vegetation alpha card Step 6)
- `buildGrassDressingBuffer`의 고정 28% 균등 확률 배치를 좌표 기반 density field로 교체.
- 넓은 patch noise와 작은 local noise를 섞어 grass 밀도가 지역별로 달라지도록 하고, 주변 8칸의 열린 GRASS 비율(`openGrass`)로 열린 잔디 영역에 약한 bias를 줌.
- `ObjectInstance` 포맷은 유지한 채 density에 따라 배치 확률, 위치 jitter, scale 범위를 다르게 적용. 인스턴스 tint/texture/card variant는 후속으로 보류.
- 기존 조건(GRASS + open sky + object 회피)과 `grassDirty` 재생성 게이트는 유지해 gameplay/save/object 렌더 경로 영향 없이 grass dressing만 변경.
- 유저 빌드 검증 결과: 실행 정상, 균등 clump 느낌이 줄고 dense/sparse patch 차이가 생김. 다음 개선은 ground dressing layer, tint/texture/card variant, wind가 핵심.

### Ground dressing layer 1차와 미학 피드백 (Vegetation Step 7)
- grass와 별도의 visual-only ground dressing buffer를 추가. 청크별 `groundPatchBuffer` / `pebbleBuffer`를 만들고, object pipeline을 재사용해 지형 렌더 후 grass 렌더 전에 그림.
- 배치 조건은 GRASS/DIRT + open sky + object 회피를 기준으로 하고, 좌표 기반 noise와 open ground bias로 patch/pebble을 결정론적으로 배치.
- 저장, 충돌, 채집, shadow caster에는 연결하지 않았다. 의도는 최종 아트가 아니라 나중에 texture/card/decal 기반 지면 디테일로 바꾸기 전 placement layer를 검증하는 것.
- 유저 빌드/스크린샷 피드백: 구조는 작동하지만 결과가 너무 못생겼다. 갈색 patch와 pebble placeholder가 크고 대비가 강해, 은은한 지면 디테일이 아니라 화면을 어지럽히는 오브젝트처럼 보인다.
- 결론: Step 7 구조는 유지 가치가 있지만 현재 geometry placeholder는 최종 방향이 아니다. 다음 작업은 patch를 크게 줄이거나 비활성화하고, grass card tint/variant 또는 낮은 대비의 texture 기반 ground detail 쪽으로 전환하는 것이 좋다.

### Ground dressing placeholder 축소 (Vegetation Step 7 cleanup)
- Step 7의 visual-only ground dressing 구조는 유지하되, 화면을 점령하던 placeholder 강도를 크게 낮춤.
- GRASS 위 ground patch 생성은 제거하고, DIRT 위에서만 아주 드물게 작은 patch가 나오도록 확률과 scale을 축소.
- pebble도 밀도와 크기를 크게 줄이고 색 대비를 낮춰 지면에 더 묻히게 조정.
- 유저 빌드/스크린샷 피드백: 이전보다 훨씬 조용해졌지만 거의 안 보일 정도로 줄었다. 현재 판단은 "없어 보이는 기준 화면"이 "못생긴 placeholder가 화면을 망치는 상태"보다 낫다는 쪽. 최종 디테일은 이후 텍스처/알파 기반 ground detail과 grass tint/card variant로 다시 채운다.

### Grass shader 기반 tint/card variation 1차 (Vegetation Step 8)
- `ObjectInstance` 포맷(`pos/scale/rot`)은 유지하고, `grass.vert`에서 인스턴스 좌표 기반 hash로 clump별 폭/높이 variation을 계산.
- `grass.vert`가 clump별 tint를 `fragTint`로 넘기고, `grass.frag`가 grass texture 색에 tint를 곱해 초록색 반복감을 완화.
- C++ vertex input, descriptor, pipeline, grass placement/density, ground dressing buffer는 변경하지 않았다. 파장은 grass shader에 한정.
- 유저 빌드 검증 결과: 셰이더 컴파일·실행 정상. grass 반복감이 약간 줄고, ground dressing cleanup 상태도 유지됨.

### 렌더링 품질/성능 기준 재정렬
- 당시 Pastel-Farm 기준으로는 **최소 GTX 1050 Ti / 1080p / 60fps**, **권장 GTX 1660 Super급**을 정리했다.
- 이 기준은 OceanVoyage 전환 이후 폐기됐다. 현재 기준은 **RTX 3060급 / 1080p~1440p / 60fps / AAA-AA급 사실적 해양 렌더링**이다.
- 과거의 스타일라이즈드/로우폴리 판단은 Pastel-Farm 기록으로만 남기며, OceanVoyage의 현재 렌더링 판단 기준으로 사용하지 않는다.
- 문서 기준을 수정: `README.md`, `DESIGN.md`, `ARCHITECTURE.md`, `VULKAN_REFERENCES.md`에서 GTX 750 Ti/통합 GPU급, 텍스처 최소화, PBR 전면 배제처럼 너무 보수적으로 보이던 문장을 정리.
- 다음 렌더링 방향은 FXAA/SMAA 실제 적용, terrain/object texture mapping, material-lite, high-quality grass(wind/LOD/variant), shadow quality options, ground dressing 텍스처화로 재정렬.
- 실행 순서도 정리: 현재 변경분 커밋 → FXAA 실제 적용 → SMAA → TextureResource helper → terrain/object texture mapping → material-lite → high-quality grass. 다음 세션은 PBR/render graph 같은 대형 시스템보다 FXAA부터 시작하는 것이 맞다.

### FXAA post AA 실제 적용 + UI 후처리 순서 분리
- Settings의 `AA OFF / FXAA / SMAA` 중 `FXAA`를 `post.frag`의 실제 FXAA 경로에 연결. `PostPushConstants`로 inverse framebuffer size와 AA mode를 fragment shader에 전달한다.
- `AA OFF`는 기존 post grading만 수행한다. `AA SMAA`는 아직 별도 SMAA가 아니며 현재는 FXAA fallback으로 동작한다. 다음 단계에서 SMAA edge/blend/neighborhood pass 또는 lookup texture/상수 테이블 방식을 설계해야 한다.
- 자체 게임 UI(메뉴/핫바/픽셀 폰트)는 scene offscreen pass에서 제거하고 post fullscreen draw 이후 같은 post render pass에서 스왑체인에 직접 렌더링한다. FXAA가 픽셀 폰트 획을 섞어 `BACK` 글자가 깨지는 문제를 해결했다.
- `createPipeline(PipelineConfig)`에 render pass override를 추가해 기본은 `m_renderPass`, UI만 `m_postRenderPass`를 사용하도록 최소 확장. ImGui DevUI와 자체 UI 모두 AA/color grading 영향 밖에서 선명하게 유지한다.
- 유저 빌드 검증 결과: `AA OFF`는 미적용, `AA FXAA`는 적용, `AA SMAA`는 현재 FXAA와 동일하게 보임. UI 깨짐 수정도 정상 확인.

### SMAA 1x 1차 후처리 패스 적용
- `AA SMAA`가 더 이상 FXAA fallback이 아니라 `smaa_edge -> smaa_blend -> smaa_neighborhood` 3-pass 후처리 경로를 사용한다.
- Iryoku SMAA 공식 LUT인 `AreaTex/SearchTex`와 `SMAA.hlsl` reference를 `third_party/smaa`에 포함했다. 라이선스 고지는 해당 파일의 MIT header를 유지한다.
- `smaa_edge.frag`는 luma edge detection, `smaa_blend.frag`는 horizontal/vertical search와 blend weight 계산, `smaa_neighborhood.frag`는 scene color와 blend texture를 이용한 neighborhood blending 및 기존 color grade를 담당한다.
- `VulkanContext`에는 SMAA intermediate render pass, edge/blend framebuffer, descriptor set, pipeline, LUT texture upload path가 추가됐다. `CMakeLists.txt`에는 새 SMAA shader compile/copy 단계가 추가됐다.
- 현재 품질은 High preset 계열 값(`threshold=0.10`, `search=16`, corner rounding 25)에 가깝지만, diagonal detection/reprojection/T2x/S2x는 제외한 1x 최소형이다.
- 유저 빌드 검증 결과: `AA SMAA`가 실제 SMAA 경로에 들어갔고 FXAA fallback과 달라졌다. 다만 1x 최소형이고 대각선 검출이 빠져 있어 체감은 아직 크지 않다.
- 추후 AA 품질을 더 올릴 때는 SMAA diagonal detection, Ultra 계열 튜닝, T2x/S2x, MSAA/alpha-to-coverage 중 어느 축을 먼저 가져갈지 별도 작업으로 결정한다.

### TextureResource RAII helper 도입 (텍스처 업로드 경로 통합)
- grass alpha 텍스처와 SMAA `AreaTex/SearchTex` LUT가 각각 staging upload → image/layout transition/copy/view 시퀀스를 중복으로 갖던 것을, `GpuBuffer`와 동일한 move-only RAII 구조체 `TextureResource`로 통합.
- `createTexture(width, height, format, bytes, size, withSampler)` 헬퍼 하나로 업로드 시퀀스를 모음. sampler는 옵션으로 처리(grass는 자체 LINEAR/CLAMP sampler 생성, SMAA LUT는 공유 `m_postSampler`를 쓰므로 sampler 미생성).
- `m_grassTex*`(4개)와 `m_smaaArea*/m_smaaSearch*`(6개) 멤버를 `m_grassTex / m_smaaAreaTex / m_smaaSearchTex` 3개의 `TextureResource`로 교체. 소멸자의 수동 destroy 블록도 `.destroy()` 호출로 정리.
- 동작 변경 없는 순수 리팩토링이며 생성 포맷·레이아웃 전환·디스크립터 바인딩·프레임 경로는 이전과 동일. 이후 terrain/object texture mapping에서 같은 헬퍼를 재사용하기 위한 토대.
- 유저 빌드 검증 결과: 컴파일·실행 정상, grass·SMAA 모두 이전과 동일 동작, Vulkan validation 에러 없음.

### SMAA edge threshold·search step Ultra 계열 튜닝 (Task #5a)
- SMAA 1x 체감이 "OFF와 차이 거의 없음" 수준이라, AA 강화의 첫 단계로 edge/blend 셰이더 상수만 Ultra 계열로 올림. 셰이더 상수 2개만 변경하고 C++/디스크립터/파이프라인/LUT는 그대로 둠.
- `smaa_edge.frag`의 `SMAA_THRESHOLD` 0.10 → 0.05 (더 약한 대비의 edge까지 검출).
- `smaa_blend.frag`의 `SMAA_MAX_SEARCH_STEPS` 16 → 32 (더 긴 edge를 추적).
- 유저 빌드 검증 결과: SMAA가 OFF 대비 edge 완화가 눈에 띄게 강해졌고 실제 적용이 체감됨.
- 대각선 계단은 1x + diagonal detection 부재 때문에 남아 있어, 다음 작업(5b)에서 `smaa_blend.frag`에 diagonal detection(`SMAACalculateDiagWeights` 계열)을 포팅해 본질적으로 개선한다.

### SMAA diagonal detection 포팅 + Ultra 프리셋 도달 (Task #5b)
- `smaa_blend.frag`에 Iryoku SMAA의 diagonal 경로(`SMAACalculateDiagWeights`, `searchDiag1/2`, `areaDiag`, `decodeDiagBilinearAccess`, `movc`)를 GLSL로 포팅. `main()`에서 north edge일 때 대각 패턴을 먼저 계산하고, 검출되면 직교 H/V 처리를 건너뜀(없으면 기존 직교 경로 fallback).
- diagonal area 데이터는 기존 `AreaTex` 우측 절반(`texcoord.x += 0.5`)에 이미 있어 새 LUT/텍스처/C++/디스크립터 변경 없이 셰이더만 수정. 1x이므로 `subsampleIndices = vec4(0)`로 단순화.
- `SMAA_MAX_SEARCH_STEPS_DIAG`를 8(High)로 1차 포팅 후 체감이 약해 16(Ultra)로 상향. 이로써 threshold 0.05 / search 32 / corner 25 / diag 16 = **원본 `SMAA_PRESET_ULTRA`와 동일**.
- 유저 빌드 검증(NO/FXAA/SMAA 비교): SMAA가 대각선 실루엣 계단을 펴주면서 FXAA처럼 뿌예지지 않고 선명도 유지. diagonal detection 동작 확인.

### SMAA edge detection을 perceptual(감마) 공간으로 — 밤 AA 수정
- 증상: 밤(어두운 장면)이 되면 SMAA가 사실상 적용되지 않음. 원인은 SMAA threshold(0.05)가 perceptual(감마) 입력 기준인데, edge 패스가 sRGB offscreen을 샘플할 때 샘플러가 linear로 디코드해 **linear 값 위에서** 검출하기 때문. linear에선 어두운 영역 대비가 압축돼 threshold 밑으로 깔림.
- 수정: `smaa_edge.frag`의 `sampleScene`에서 `pow(c, 1/2.2)`로 perceptual 복원 후 luma edge detection. edge 패스는 edge 플래그만 출력하므로 색감·블렌딩(linear 유지)에는 영향 없음.
- 대안 검토: "grade/tonemap을 AA 앞으로 옮기는 구조 변경(C안)"은 현재 grade가 약한 컬러 그레이딩(톤매퍼 아님)이라 밤을 못 고치고, 감마 인코딩 전체 이동은 swapchain 색공간까지 건드리는 큰 변경이라 보류. 실제 HDR/톤매핑 도입 시 `Scene(HDR)→Tonemap→AA→Present`로 정식 적용 예정.
- FXAA도 동일하게 linear에서 luma를 보므로 밤에 약했다. 후속 커밋에서 `post.frag`에 `lumaPerceptual()`를 추가해 FXAA의 edge/방향 판단 luma만 perceptual 감마로 계산하고, 블렌딩 출력 색은 linear로 유지(이중 감마 인코딩 방지). 유저 빌드 검증: FXAA 밤 AA 정상, 낮 색감 변화 없음.
- 유저 빌드 검증: 밤 장면에서도 큐브/타일/오브젝트 경계 AA 정상 적용 확인, 낮 장면 정상.

### Terrain texture array 1차 배선 (Task #3a)
- `ChunkVertex`에 면별 `uv`와 `layer`를 추가하고, `buildChunkBuffer`에서 각 보이는 면의 0..1 UV와 `tileFaceLayer(TileType, isTop)` 결과를 함께 emit하도록 변경했다.
- terrain albedo는 atlas 대신 `sampler2DArray`로 시작했다. 경계 mip 번짐을 피하고, 타일 타입/면별 layer 확장이 단순하며, `TextureResource` 수명 모델을 그대로 사용할 수 있기 때문이다.
- `TERRAIN_TEX_LAYERS = 9`: GRASS top, GRASS side, DIRT, STONE, WOOD, LEAVES, FARMLAND, WHEAT, WATER를 절차 layer로 배정했다. 이번 단계의 layer art는 타입 구분과 경로 검증을 위한 낮은 대비의 seamless grayscale grain이다.
- `createTextureArray(...)`와 `createTerrainTextureArray()`를 추가하고, `m_terrainTex` 생성/소멸을 `VulkanContext` 수명에 연결했다. descriptor set layout/pool/set update에는 binding 3 terrain texture array를 추가했다.
- `chunk.vert`는 UV/layer를 fragment로 전달하고, `chunk.frag`는 terrain layer를 샘플해 `fragColor * albedo * lighting`으로 합성한다. 기존 vertex color는 타일 색조와 AO tint로 유지된다.
- `object.vert`는 `fragLayer = -1.0` sentinel을 출력해 공유 `chunk.frag`에서 텍스처 샘플을 건너뛰게 했다. 따라서 tree/rock/workbench/fence 등 오브젝트 메시와 vertex 포맷은 이번 단계에서 건드리지 않았다.
- 유저 빌드 검증 결과: 지형 텍스처 배열 경로가 정상 동작하고, 기존 색조/AO와 오브젝트·grass·shadow·UI·AA 경로는 유지되는 것으로 확인했다.

### Terrain texture art 절차 패턴 1차 튜닝 (Task #3b)
- `createTerrainTextureArray()` 내부만 수정해 새 이미지 파일/의존성 없이 9개 terrain layer의 절차 패턴을 구분했다. texture resource, descriptor, shader, vertex format은 그대로 유지했다.
- texture 크기를 32×32 → 64×64로 올리고, 레이어별로 grass blade/clump, grass side strata, dirt clod/speckle, stone mottling/crack, wood grain, leaves mottling, farmland furrow, wheat stalk, water ripple 계열의 낮은 대비 패턴을 적용했다.
- 텍스처는 여전히 albedo라기보다 material mask에 가깝게 설계했다. `chunk.frag`의 기존 `fragColor * albedo * lighting` 구조에서 vertex color가 색조와 AO를 계속 주도하도록 했다.
- 유저 스크린샷 검증 결과: terrain layer 패턴이 보이고 grass/dirt/farmland/water 구분이 살아났다. 물은 패턴이 규칙적으로 보일 수 있어 ripple 대비/빈도를 낮춰 임시 placeholder로 정리했다.
- water는 이후 별도 water pass 또는 water material 작업에서 다시 다룬다. 이번 단계의 목표는 실제 texture map 도입 전 UV/layer/tint/반복감 기준을 잡는 것이다.

### Object texture mapping 1차 배선 (Task #3c)
- 새 texture resource나 이미지 파일 없이, 기존 terrain `sampler2DArray`를 공유 material layer로 재사용해 StaticProp 오브젝트에도 텍스처 샘플 경로를 열었다.
- object pipeline vertex input에 `ChunkVertex.uv`/`layer`를 location 6/7로 추가하고, `object.vert`가 더 이상 `fragLayer = -1` sentinel을 강제하지 않고 정점 UV/layer를 `chunk.frag`로 전달하게 했다.
- `createObjectMeshes()`에서 tree trunk/workbench/fence는 WOOD layer, tree canopy는 LEAVES layer, rock/stone fence는 STONE layer를 받도록 정점 데이터를 채웠다. vertex color는 기존처럼 색조/tint 역할로 유지한다.
- ground patch, pebble, legacy grass clump 같은 visual-only dressing mesh는 `layer = -1`로 명시해 기존 vertex color-only 표현을 유지했다.
- `chunk.frag`와 `ChunkVertex` 주석은 terrain 전용이 아니라 material texture-array layer 의미로 정리했다. shadow object pass는 position만 읽으므로 변경하지 않았다.
- 유저 스크린샷 검증 결과: 나무/울타리/작업대의 wood grain, 바위/돌담의 stone layer가 정상적으로 보이고, 지형·grass·shadow·UI·AA 경로는 유지되는 것으로 확인했다.

### Authored texture loading 토대 추가 (Task #4a)
- `third_party/stb/stb_image.h`를 추가해 PNG/JPG 등 이미지 파일을 RGBA8 픽셀로 읽을 수 있게 했다. 새 런타임 DLL이나 패키지 매니저 의존성 없이 단일 헤더만 저장소에 포함한다.
- `CMakeLists.txt`에 `assets` post-build copy를 추가했다. 빌드 후 실행 파일 옆에 `assets/textures` 경로가 생기며, 이후 실제 텍스처 이미지를 넣어도 실행 기준 상대 경로가 유지된다.
- `createTextureFromFile(path, withSampler)`를 추가해 파일 로드 결과를 기존 `TextureResource`/`createTexture(...)` 업로드 경로로 연결했다. Vulkan image/view/sampler 수명 모델은 기존과 동일하다.
- `createGrassTexture()`는 `assets/textures/grass.png`가 있으면 파일 텍스처를 먼저 사용하고, 없으면 기존 절차 grass alpha texture를 그대로 생성한다. 따라서 이번 단계만으로 화면 변화는 없어야 한다.
- 유저 빌드 검증 결과: `out/build/x64-Debug/assets` 폴더가 생성되어 asset copy 토대가 정상 동작하는 것을 확인했다. 실제 texture map 선정과 material-lite 상수, mipmap/sampler 정책은 후속 작업으로 남긴다.

### Authored terrain texture override 1차 적용 (Task #4b)
- `createTerrainTextureArray()`가 먼저 절차 material mask를 생성한 뒤, `assets/textures/terrain/*.png`가 있으면 layer별로 RGBA8 파일 이미지를 덮어쓰게 했다.
- 파일 매핑은 `grass_top`, `grass_side`, `dirt`, `stone`, `wood`, `leaves`, `farmland`, `wheat`, `water` 9개 layer다. 현재 저장소에는 water를 제외한 8개 `1024×1024 PNG` Color texture를 추가했다.
- 첫 번째로 발견한 authored terrain 이미지 크기를 texture array 크기로 삼고, 크기가 다른 파일은 해당 layer만 건너뛰며 절차 fallback을 유지한다. 모든 layer가 누락되면 기존 64×64 절차 texture array로 동작한다.
- `water.png`는 의도적으로 추가하지 않았다. 물은 현재 절차 ripple fallback을 유지하고, 이후 전용 water pass/material에서 별도 처리한다.
- 유저 빌드/스크린샷 검증 결과: terrain authored texture가 실제로 적용됐다. 현재 `fragColor * texture` 구조 때문에 색과 노이즈가 강하게 먹을 수 있으므로, 다음 개선은 texture strength 또는 stylized albedo 보정으로 분리한다.

### Texture tone 안정화 1차 (Task #4c)
- `chunk.frag`의 terrain/object material 합성을 `fragColor * rawTexture`에서 `fragColor * materialDetail`로 바꿨다.
- `sampleMaterialDetail()`이 texture luma를 중립 밝기 디테일로 변환하고, texture chroma는 약하게만 섞는다. vertex color가 Pastel Farm의 주 색감을 유지하고, authored texture는 표면 질감 역할에 머물게 하는 목적이다.
- 첫 조정은 너무 약해 grass/dirt/stone 질감이 덜 읽혔고, 최종값은 detail 강도 0.80, detail clamp 0.68~1.26, chroma mix 0.24로 정했다.
- 유저 빌드/스크린샷 검증 결과: 첫 authored texture 적용 때처럼 실사 노이즈가 화면을 잡아먹지 않으면서, 흙과 지형 질감은 은은하게 남았다. 전역 안정화 값으로는 유지하고, layer별 strength는 필요가 확인되면 후속 작업으로 분리한다.

### Layer별 texture strength 1차 (Task #4d)
- `chunk.frag`에 `materialTextureStrength(layer)`를 추가해 terrain/object가 공유하는 material layer별 texture 영향도를 다르게 조절했다.
- grass top/leaves는 낮게 유지해 바닥과 캐노피가 실사 노이즈로 지저분해지지 않게 하고, dirt/farmland/stone은 조금 더 높여 질감이 읽히게 했다. wood/wheat는 중간값, water fallback은 낮은 값으로 둔다.
- 현재 값: grass top 0.62, grass side 0.82, dirt 1.05, stone 1.15, wood 0.90, leaves 0.58, farmland 1.05, wheat 0.90, water 0.35.
- 유저 빌드/스크린샷 검증 결과: 흙/밭/돌의 texture 존재감은 살아났고 grass top은 조용하게 유지됐다. grass 바닥은 이후 grass card density/variant/wind와 ground dressing으로 채우는 방향이라, 현재 값은 유지한다.

### High-quality grass 1차 튜닝 (Task #5a)
- 기존 grass alpha card 경로를 유지한 채, X자 2-card clump를 120도 간격의 3-card clump로 바꿨다. 인스턴스 포맷, descriptor, shadow caster, 저장/충돌 규칙은 변경하지 않았다.
- `buildGrassDressingBuffer()`에서 grass 배치 확률을 `0.06 + density * 0.40` → `0.08 + density * 0.48`로 올리고, scale/jitter/variant 범위를 조금 넓혀 빈 잔디 바닥을 card가 더 채우게 했다.
- 유저 빌드/스크린샷 검증 결과: grass가 이전보다 풍성해졌고, DevUI 기준 scene GPU 시간이 약 1.0ms 수준으로 유지됐다. 일부 균등 배치 느낌은 남아 있으므로 다음 개선은 density patch 대비, wind sway, 거리 LOD/fade, card/tint variant로 본다.

### Reference grass blade-field 1차 (Task #5b)
- 레퍼런스 스크린샷 기준으로 기존 3-card clump는 "큰 풀 오브젝트를 반복 배치한" 느낌이 강해 최종 방향으로 부적합하다고 판단했다. grass 전용 파이프라인, alpha test, instance buffer, density field, dirty gate는 유지하고, **아트 모델**만 낮고 촘촘한 blade-field로 전환했다.
- `FrameRenderData`와 UBO에 `gameTime`/`animationParams.x`를 추가해 grass shader가 시간 기반 wind sway를 쓸 수 있게 했다. 다른 렌더 경로는 기존 UBO 필드를 그대로 사용한다.
- `m_grassCardMesh`를 큰 3-card clump에서 여러 개의 작고 얇은 blade card cluster로 교체했다. 첫 시도는 너무 가늘고 어두워 거의 보이지 않았고, 후속 보정에서 card 폭/높이/개수, texture alpha, tint, density를 키워 화면에서 풀밭으로 읽히게 조정했다.
- `createGrassTexture()`의 절차 RGBA mask를 두꺼운 tuft 중심에서 얇은 grass blade 중심으로 다시 그렸다. base/tip 색 대비와 alpha를 올려 현재 카메라 거리에서도 사라지지 않게 했다.
- `buildGrassDressingBuffer()`는 dense patch에서 한 타일에 여러 작은 cluster를 만들 수 있게 바꿨다. sparse 구간은 여전히 비워 전체가 균일한 도장처럼 보이지 않게 유지한다.
- `grass.vert`는 짧은 blade에 맞춰 약한 wind sway, per-instance tint, 거리 fade 값을 계산한다. `grass.frag`는 fade 기반 alpha cutoff를 사용하되, 가까운 풀은 충분히 남도록 cutoff를 낮췄다.
- 유저 빌드/스크린샷 검증 결과: 첫 blade-field 시도는 거의 보이지 않았으나, 2차 보정 후 이전보다 풀밭 면으로 읽히는 결과가 나왔다. DevUI 기준 scene GPU 시간은 약 0.56ms 수준으로 여유가 있다. 다음 격차는 grass 자체보다 바닥 grass material, ground breakup, material-lite(AO/roughness/normal 선별 사용), 조명/후처리 쪽에서 줄인다.
- 에셋 맵 운용 판단: `Color`는 즉시 바닥 material에 유효하고, `AmbientOcclusion`/`Roughness`는 material-lite에서 약하게 쓰는 후보. `Normal`은 로우폴리/플랫 셰이딩과 충돌할 수 있어 약한 강도로 검토한다. `Displacement`는 현재 구조에서 실제 변위로 바로 쓰지 않고, height mask/ground detail/density 보조 데이터로 검토한다.

---

## 게임 설계 메모

> 이 절은 현재 방향만 요약한다. 초기의 Minecraft식 복셀 설치/파괴 아이디어와 구현 순서 메모는 위 구현 기록에 역사로 남기고, 현재 설계 판단은 `DESIGN.md`와 `ARCHITECTURE.md`를 기준으로 한다.

### 현재 게임 방향

Pastel Farm은 커스텀 Vulkan 엔진 기반의 고품질 스타일라이즈드 로우폴리 농사·라이프심이다.
현재 방향은 **Stardew-style 농사/채집/제작/건축 + 고정 아이소메트릭 시점 + authored 느낌의 세계**다.

- 지형은 불변이다. 플레이어는 복셀 블록을 자유 설치/파괴하지 않는다.
- 자연물과 건축물은 `ObjectType` 기반 StaticProp 레이어에서 처리한다.
- 나무/돌은 도구로 채집하고, 제작한 작업대/울타리/돌담은 오브젝트로 설치·철거한다.
- 농사 규칙과 설치/충돌/저장은 grid를 따른다.
- 자연 시각 표현은 grass alpha card와 dressing layer로 grid감을 줄인다.

### 현재 월드/렌더 구조

```
World
├─ Terrain    32×32×8 청크 기반 voxel 지형
├─ TileState  growthStage / lastUpdatedDay / watered
├─ StaticProp ObjectType(tree/rock/workbench/fence/stone fence)
└─ Dressing   grass blade-field alpha card 등 저장하지 않는 시각 레이어
```

- 청크는 스트리밍 단위이며, 현재 지형은 FBM 기반 절차 생성이다.
- save v2는 수정 청크의 타일, TileState 일부, 오브젝트를 저장한다.
- 렌더러는 청크 메시, 오브젝트 인스턴싱, grass alpha card, player/drop, post pass(FXAA/SMAA 1x), post 이후 UI overlay를 분리해 그린다.
- 다음 비주얼 개선은 ground grass texture/detail, material-lite(AO/roughness/normal 선별), grass density/색/LOD 후속 튜닝, ground dressing 텍스처화, water 전용 표현이 핵심이다. 필요하면 SMAA T2x/S2x나 MSAA/alpha-to-coverage는 별도 품질 작업으로 분리한다.

---

## Vulkan 개념 정리

### 왜 Vulkan인가

OpenGL은 드라이버가 렌더링 과정을 대부분 알아서 처리한다.
Vulkan은 드라이버가 아무것도 안 해준다. GPU가 뭘 어떻게 할지 **전부 직접 지정**해야 한다.

대신 얻는 것:
- CPU 오버헤드 최소화
- 멀티스레드 렌더링 지원
- 예측 가능한 퍼포먼스 (정해진 성능 예산 안에서 품질을 올리기 위해 중요)

---

### Vulkan 초기화 순서

`VulkanContext` 생성자에서 이 순서로 호출된다:

| 순서 | 함수 | 설명 |
|------|------|------|
| 1 | `createInstance()` | Vulkan 자체를 켬. 앱 정보, 사용할 익스텐션 등록 |
| 2 | `setupDebugMessenger()` | Validation Layer 에러/경고를 콘솔에 출력하는 콜백 등록 |
| 3 | `createSurface()` | GLFW 창과 Vulkan을 연결하는 Surface 생성 |
| 4 | `pickPhysicalDevice()` | GPU 선택 (Discrete GPU 우선, 없으면 Integrated) |
| 5 | `createLogicalDevice()` | 선택한 GPU에 논리적 연결 생성, Queue 핸들 획득 |
| 6 | `createSwapchain()` | 화면 출력용 이미지 버퍼 시스템 생성 |
| 7 | `createImageViews()` | 스왑체인 이미지를 읽고 쓸 수 있는 "창구" 생성 |
| 8 | `createRenderPass()` | 어떤 포맷으로, 어떤 순서로 그릴지 명세 |
| 9 | `createGraphicsPipeline()` | 렌더링 파이프라인 전체 설계도 생성 |
| 10 | `createFramebuffers()` | 실제로 그릴 대상 프레임버퍼 생성 |
| 11 | `createCommandPool()` | GPU에 보낼 명령어를 담는 저장소 생성 |
| 12 | `createVertexBuffer()` | 정점 데이터를 GPU 메모리에 업로드 |
| 13 | `createIndexBuffer()` | 인덱스 데이터를 GPU 메모리에 업로드 |
| 14 | `createCommandBuffers()` | 명령어 버퍼 할당 |
| 15 | `createSyncObjects()` | CPU-GPU 동기화 객체 생성 |

---

### 그래픽스 파이프라인

"정점 데이터가 화면 픽셀로 바뀌는 과정의 설계도."
`createGraphicsPipeline()`에서 한 번에 전부 정의하고, **만들고 나면 변경 불가.**
설정이 달라지면 파이프라인을 새로 만들어야 한다.

```
[정점 데이터 (kVertices)]
        ↓
Vertex Input       → Vertex 구조체 레이아웃 등록
                     stride = sizeof(Vertex), location 0 = pos, location 1 = color
        ↓
Input Assembly     → 정점을 어떻게 묶을지
                     TRIANGLE_LIST = 3개씩 삼각형
        ↓
Vertex Shader      → 정점마다 실행 (triangle.vert.spv)
                     현재: pos 그대로 통과, color를 fragment로 전달
        ↓
Rasterization      → 삼각형 내부를 픽셀로 채움
                     cullMode = BACK → 뒷면(카메라 반대) 컬링
        ↓
Fragment Shader    → 픽셀마다 실행 (triangle.frag.spv)
                     현재: vertex shader에서 받은 color를 그대로 출력
        ↓
Color Blend        → 투명도 처리 (현재 블렌딩 없음, 그냥 덮어씌움)
        ↓
[화면]
```

---

### 스왑체인

화면에 바로 그리면 GPU가 그리는 도중 모니터가 읽어버려서 찢김(tearing)이 생긴다.
스왑체인은 이미지를 2~3개 만들어 교대로 사용해 이 문제를 해결한다.

```
이미지 A: GPU가 렌더링 중
이미지 B: 모니터에 출력 중
→ A 완성되면 swap → A가 화면에, B는 다음 프레임 렌더링 대상
```

- **FIFO** (vsync): 출력 완료 후 교체. 찢김 없음, 레이턴시 있음
- **Mailbox** (triple buffer): 새 프레임이 오면 대기 중인 것 교체. 레이턴시 낮음

현재 코드에서는 Mailbox 우선, 없으면 FIFO로 폴백한다.

**창 크기가 바뀌면** 스왑체인 전체를 재생성해야 한다 → `recreateSwapchain()`.

---

### 버텍스 버퍼

셰이더에 하드코딩하던 정점 데이터를 CPU → GPU 메모리로 옮기는 과정.

```cpp
vkMapMemory(...);                          // GPU 메모리를 CPU 주소 공간에 매핑
memcpy(data, kVertices.data(), size);      // 복사
vkUnmapMemory(...);                        // 매핑 해제
```

**메모리 종류:**

| 종류 | 설명 | 현재 사용 |
|------|------|-----------|
| `HOST_VISIBLE` | CPU가 직접 읽고 쓸 수 있음. 느림 | ✅ 현재 |
| `DEVICE_LOCAL` | GPU 전용 메모리. 빠르지만 CPU 직접 접근 불가 | 나중에 |

**현재 엔진 적용 상태:**
정적 데이터(메시, 인덱스)는 **Staging Buffer** 방식을 도입하여 `DEVICE_LOCAL`에 배치해 렌더링 성능을 극대화했고, 매 프레임 값이 변하는 동적 데이터(인스턴스 버퍼)는 `HOST_VISIBLE`에 남겨두어 CPU-GPU 간 데이터 전송 오버헤드를 최소화하는 투트랙 전략을 사용 중이다. 전송 완료 대기는 `vkQueueWaitIdle` 대신 `VkFence`를 사용해 해당 전송만 선택적으로 대기한다.

---

### 인스턴싱 개념

같은 메시를 여러 위치에 그릴 때, 드로우콜을 N번 호출하는 대신 인스턴스 데이터를 GPU에 한 번에 올리고 `instanceCount=N`으로 1번만 호출한다.

```
// 일반 방식: 드로우콜 N번
for each tile:
    updateUBO(tilePos)
    vkCmdDrawIndexed(..., 1, ...)

// 인스턴싱: 드로우콜 1번
uploadInstanceBuffer(allPositions)
vkCmdDrawIndexed(..., N, ...)
```

버텍스 버퍼(binding 0)는 `VERTEX`당 한 번 읽고,
인스턴스 버퍼(binding 1)는 `INSTANCE`당 한 번 읽는다.
셰이더에서 두 데이터를 합쳐 최종 위치를 계산한다.

타일 수가 늘어도 드로우콜은 1번이므로 CPU 부하가 거의 없다.

---

### 플랫 셰이딩 개념

스무스 셰이딩은 정점 사이 법선을 보간해서 면이 부드럽게 보인다.
플랫 셰이딩은 보간 없이 삼각형 전체가 하나의 색상 — 로우폴리 스타일의 핵심.

GLSL `flat` qualifier를 쓰면 provoking vertex(삼각형의 첫 번째 정점)의 값을 그대로 사용한다.

```
// 스무스: 정점마다 다른 법선 → 면 안에서 보간
out vec3 fragNormal;

// 플랫: 보간 없음 → 면 전체 동일
flat out vec3 fragNormal;
```

플랫 셰이딩에서는 정점을 면끼리 **공유할 수 없다.**
같은 꼭짓점이라도 면마다 법선이 다르기 때문에 별도 정점이 필요하다.
→ 큐브: 8 정점(스무스) → 24 정점(플랫, 면 6 × 4)

**Lambert 조명:**
```
float diff  = max(dot(normal, lightDir), 0.0);
float light = ambient + diff * diffuseStrength;
color       = baseColor * light;
```

---

### 궤도 카메라 개념

구면 좌표계로 카메라 위치를 계산한다:

```
camPos.x = target.x + distance * cos(pitch) * cos(angle)
camPos.y = target.y + distance * cos(pitch) * sin(angle)
camPos.z = target.z + distance * sin(pitch)
view = lookAt(camPos, target, up)
```

- `angle` (Q/E로 제어): 수평 공전 각도
- `pitch` (고정): 내려다보는 각도
- `distance` (고정): 타겟까지 거리

자유 시점 카메라와 달리 항상 타겟을 바라보므로,
플레이어 위치만 `m_orbitTarget`에 넘기면 카메라가 자동으로 따라간다.

---

### Depth Buffer

3D에서 여러 오브젝트가 겹칠 때 어느 픽셀이 앞에 있는지 판별하는 버퍼.
없으면 나중에 그려진 오브젝트가 무조건 앞에 나온다 (화가 알고리즘 문제).

```
픽셀을 그릴 때:
  새 픽셀의 depth < 저장된 depth  → 그리고 depth 갱신
  새 픽셀의 depth >= 저장된 depth → 버림
```

Depth Buffer는 스왑체인 이미지와 같은 크기여야 하므로 창 리사이즈 시 함께 재생성한다.

Render Pass에 depth attachment를 추가하고, Framebuffer에 depth image view를 연결하고,
Pipeline에 `VkPipelineDepthStencilStateCreateInfo`로 depth test를 활성화해야 한다.
세 군데 모두 연결해야 작동한다.

---

### UBO와 Descriptor

**UBO (Uniform Buffer Object)** 는 매 프레임 CPU에서 GPU 셰이더로 데이터를 넘기는 방법이다.
Vertex Buffer가 "정점마다 다른 데이터"라면, UBO는 "모든 정점에 공통으로 적용되는 데이터"다.

```
CPU (C++ 코드)          GPU (셰이더)
UniformBufferObject  →  layout(binding=0) uniform UniformBufferObject { ... } ubo;
  model matrix            gl_Position = ubo.proj * ubo.view * ubo.model * pos;
  view  matrix
  proj  matrix
```

Vulkan에서 UBO를 셰이더에 넘기려면 **Descriptor** 시스템을 거쳐야 한다:

```
Descriptor Set Layout  → "binding 0에 UBO가 있다"는 설계도
Descriptor Pool        → Descriptor Set을 찍어낼 메모리 풀
Descriptor Set         → 실제 버퍼와 셰이더 바인딩을 연결하는 객체
```

파이프라인 레이아웃에 Descriptor Set Layout을 등록하고,
`vkCmdBindDescriptorSets`로 드로우 전에 바인딩한다.

**MVP 행렬:**

| 행렬 | 역할 |
|------|------|
| Model | 오브젝트를 월드 공간에 배치 (이동, 회전, 스케일) |
| View | 카메라 위치/방향에 따라 월드를 카메라 공간으로 변환 |
| Projection | 카메라 공간을 클립 공간으로 변환 (원근감 적용) |

`gl_Position = proj * view * model * vertex` 순서로 곱한다 (오른쪽부터 적용).

**GLM과 Vulkan의 Y축 차이:**
GLM은 OpenGL 기준으로 만들어져서 Y축이 위가 양수다.
Vulkan은 Y축이 위가 음수(화면 아래가 +Y). 그래서 `proj[1][1] *= -1`로 Y를 뒤집는다.
이 Y반전으로 winding order가 뒤집히므로 `frontFace = COUNTER_CLOCKWISE`로 설정한다.

---

### 인덱스 버퍼

정점을 재사용해서 메모리를 절약하는 방식.

```
삼각형 2개로 사각형:
  정점 버퍼만 쓰면 → A B C A C D (6개, A·C 중복)
  인덱스 버퍼 쓰면 → 정점 A B C D (4개) + 인덱스 [0,1,2, 0,2,3]
```

메시가 복잡해질수록 절약량이 커진다. 3D 큐브(꼭짓점 8개, 삼각형 12개)부터 체감된다.

---

### 매 프레임 루프 (`drawFrame`)

```
1. vkWaitForFences          → 이전 프레임(같은 슬롯)이 끝날 때까지 대기
2. vkAcquireNextImageKHR    → 스왑체인에서 "지금 그릴 이미지" 가져오기
3. recordCommandBuffer       → GPU에 보낼 명령어 기록
      vkCmdBeginRenderPass   → 렌더 패스 시작, 배경색 클리어
      vkCmdBindPipeline      → 사용할 파이프라인 지정
      vkCmdBindVertexBuffers → 정점 버퍼 바인드
      vkCmdBindIndexBuffer   → 인덱스 버퍼 바인드
      vkCmdDrawIndexed       → "인덱스 기준으로 그려라"
      vkCmdEndRenderPass     → 렌더 패스 종료
4. vkQueueSubmit             → GPU에 명령 제출
5. vkQueuePresentKHR         → 완성된 이미지를 화면에 출력
6. m_currentFrame = (m_currentFrame + 1) % 2  → 다음 슬롯으로
```

**`MAX_FRAMES_IN_FLIGHT = 2`**

CPU와 GPU가 서로 다른 프레임을 동시에 처리한다.
CPU가 프레임 1 명령을 기록하는 동안 GPU는 프레임 0을 렌더링 중. GPU가 놀지 않아서 효율적.

**동기화 객체:**

| 객체 | 용도 |
|------|------|
| `Semaphore (imageAvailable)` | 스왑체인 이미지 준비 완료 신호 |
| `Semaphore (renderFinished)` | 렌더링 완료 신호 (present 전에 대기) |
| `Fence (inFlight)` | CPU가 GPU 완료를 기다릴 때 사용 |

세마포어는 GPU-GPU 동기화, 펜스는 CPU-GPU 동기화에 사용한다.
