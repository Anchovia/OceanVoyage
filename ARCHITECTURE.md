# ARCHITECTURE — Pastel Farm Engine

> 이 문서는 **엔진이 어떻게 구성돼 있고 왜 그렇게 했는지 + 기술적 장기 방향**을 다룬다.
> 게임 기획은 `DESIGN.md`, 변경 이력은 `DEVLOG.md`, 기능 스냅샷·빌드는 `README.md`.
> 외부 Vulkan 예제 레퍼런스 운용 기준은 `VULKAN_REFERENCES.md`.
>
> **갱신 시점:** 구조가 바뀔 때만(새 시스템 / 리팩토링 / 파이프라인 변경). 매 작업마다 갱신하지 않는다.
> **태그:** `[구현됨]` = 현재 코드에 존재 · `[계획]` = 합의된 방향(아직 구현 안 됨).

---

## 설계 철학

- 고품질 스타일라이즈드 / 로우폴리 / 플랫 셰이딩 — 로우폴리는 저품질 제약이 아니라 미학적 선택
- 성능 기준: 최소 GTX 1050 Ti / 1080p / 60fps, 권장 GTX 1660 Super급. 초저사양 데모가 아니라 **최적화된 고품질 상용 게임**이 목표
- 최신 기법은 선별적으로 사용한다: 텍스처 매핑, material-lite, 고품질 식생, AA, shadow 품질 옵션, post 효과를 성능 예산 안에서 적극 도입
- Vulkan 직접 제어, 게임과 엔진 동시 개발
- **품질 기본값 (중요 — 반복 위반된 항목)**: 비주얼/렌더링 작업은 *제대로 된 고품질 기법*을 1660 Super 예산 안에서 **기본값**으로 택한다. 기능 제거·가짜·축소(그림자 끄기, blob 그림자, 64² 텍스처 고수, CSM "오버킬" 회피, 해상도 대신 PCF만 키우기 등)를 먼저 택하지 않는다. 현재 코드가 미니멀해 보이는 건 placeholder이지 품질 목표가 아니다. CSM·밉맵·이방성·고해상도 shadow map·soft shadow는 anti-goal이 아니라 **권장 품질 기법**이다
- 렌더러 원칙: **명시적 제어 + 유지보수성** (단, "단순함"은 *코드 구조*에 적용되는 원칙이지 *비주얼 품질을 낮추라는 뜻이 아니다*)
- **정공법 우선 (안정성·확장성·호환성 — 중요)**: 검증된 표준·Vulkan 관용(idiomatic) 구현을 택한다 — 대부분의 엔진/레퍼런스(SaschaWillems·Khronos Vulkan-Samples·LearnOpenGL, `VULKAN_REFERENCES.md`)가 실제로 쓰는 방식. 도전적·실험적·비표준 우회로 표준 기법의 일부를 **스킵·무시·컷오프하지 않는다** — 그러면 깨지기 쉽고 호환·확장이 나빠진다. 표준 기법은 *제대로, 완전하게* 구현하고, 나중에 기능을 여러 개 붙여도 자연스럽게 맞물리는 구조를 우선한다. "단순함"은 *표준 정공법*을 쓰라는 뜻이지 *새로운 지름길을 발명하라는 게 아니다*

---

## 모듈 구조 [구현됨]

```
src/
├─ platform/   Window(GLFW RAII), InputManager (입력 폴링 일원화)
├─ game/       Camera, Player, GameState (이동·충돌·시간·농사 규칙)
├─ world/      Chunk, World(청크 라우팅·save/load), TerrainGen(FBM)
└─ renderer/   VulkanContext(_Init/_Frame/_Chunk), Types, Frustum
```

- 책임 분리: **World = 게임 데이터, VulkanContext = 렌더링**. 렌더러는 "월드가 주는 것을 그린다".

---

## 렌더러 [구현됨]

- Vulkan: instance / device / swapchain / render pass / pipelines / sync
- **파이프라인**: 공유 빌더 `createPipeline(PipelineConfig)`로 scene 계열(player/selector/drop·chunk·object·grass)과 UI overlay pipeline을 생성하고, post는 별도 fullscreen pipeline으로 처리. `AA OFF/FXAA`는 기존 post pipeline, `AA SMAA`는 `smaa_edge → smaa_blend → smaa_neighborhood` 3-pass 후처리 체인을 사용한다. UI pipeline은 post render pass에 맞춰 생성되어 AA/color grading 이후 스왑체인 위에 그려진다. shadow 계열(청크/오브젝트/플레이어)은 depth-only 별도 파이프라인.
- viewport/scissor = **dynamic state** (리사이즈 시 파이프라인 재생성 불필요)
- **청크 메시**: Hidden Face Culling, 청크별 vertex/index 버퍼, dirty만 리빌드(프레임당 N개 제한)
- **컬링**: 청크 AABB frustum culling (메인패스 + shadow 라이트 프러스텀)
- **오브젝트**: `ObjectType`별 공유 메시 + 청크별 타입 그룹 인스턴스 버퍼(tree/rock/workbench/fence/stone fence). 오브젝트 변경 시에만 `objectsDirty`로 재빌드
- **식생/지면 dressing**: 외부 grass blade Color/Opacity 텍스처 + 낮은 blade-field card cluster + 청크별 grass instance buffer + 좌표 기반 density field + shader 기반 grass tint/wind/fade. 별도 ground dressing buffer로 잔돌/패치 placement도 검증 중. 저장하지 않는 시각 dressing layer가 기본이며, 현재는 가까운 grass card만 alpha-tested shadow pass에 넣는 실험 상태
- **조명 스택**: ambient + sun diffuse(dayFactor) + shadow + fog (4-layer)
- **그림자**: 2048² shadow map, 3×3 PCF, 캐스터=청크+`ObjectDef.castShadow` 오브젝트+플레이어+실험적 grass alpha caster, 밤엔 shadow geometry draw 스킵
- **day/night**: `timeOfDay`로 태양 방향/하늘색/안개색/조도 변화
- 색/재질: 지형과 StaticProp 오브젝트는 `sampler2DArray` 기반 material layer + vertex color tint를 사용한다. terrain layer는 절차 패턴 fallback 위에 `assets/textures/terrain/*.png` authored Color texture를 layer별 override로 적용할 수 있고, 오브젝트는 WOOD/LEAVES/STONE layer를 재사용하는 1차 mapping을 적용했다. grass는 alpha texture이며, `assets/textures/grass.png`가 있으면 `stb_image` 기반 파일 텍스처로 교체할 수 있다. 다음 단계에서 material-lite(albedo + tint + roughness/specular 계열 상수), texture strength/stylized albedo 보정, water 전용 표현을 확장한다
- **DevUI / 프로파일링**: `PASTEL_DEV_BUILD`에서 Dear ImGui F3 패널을 post pass 위에 렌더링. 자체 게임 UI도 post AA 이후에 렌더링해 픽셀 폰트가 AA에 의해 깨지지 않게 한다. `VkQueryPool` timestamp로 total/shadow/scene/post/imgui GPU 구간 시간을 표시

---

## 동기화 모델 [구현됨]

- `MAX_FRAMES_IN_FLIGHT = 2`
- `imageAvailable` / `inFlight` = 프레임별, `renderFinished` = **스왑체인 이미지별** + `imagesInFlight` 추적 (semaphore 재사용 위반 방지)
- player / selector / UI 버퍼 + UBO = **프레임별 복제** (frame-in-flight data race 방지)
- **deferred deletion 큐**: GPU가 아직 읽는 청크 버퍼를 N프레임 뒤 안전 해제

---

## 월드 [구현됨]

- 청크 `unordered_map<ivec2, Chunk>`, 32×32×8, load/unload radius 기반 **스트리밍**
- 절차 지형: FBM noise 2채널(height/biome), 좌표 기반 **결정론적**
- `TileState`: `growthStage`, `lastUpdatedDay`, `watered`(매일 마름; v3부터 세이브에 영속)
- 오브젝트 레이어: **terrain(voxel) ≠ object(prop)** 분리. 자연물(tree/rock)과 설치물(workbench/fence/stone fence)을 같은 StaticProp 경로로 처리
- save/load: **수정 청크 + 인벤토리 + 드롭**을 바이너리 저장(v3). 청크는 타일 + `TileState`(growthStage·lastUpdatedDay·watered) + 오브젝트를 직렬화해 설치물 유지·채집물 respawn 방지를 처리. **atomic write**(temp→rename)와 **로드 검증**(count/objCount/enum 범위, 실패 시 무변경)로 견고화. 미수정 청크는 좌표 결정론으로 재생성

---

## 기술 방향 [계획]

> **렌더링 목표 재정렬.** Pastel Farm은 초저사양 로우폴리 데모가 아니라, GTX 1050 Ti 최소 / GTX 1660 Super 권장 기준에서 고품질 스타일라이즈드 화면을 목표로 한다. 최적화·모듈성은 계속 핵심이지만, 품질을 낮추기 위한 보수주의는 피한다.

### 우선순위 Tier (현재)
**Tier 1 — 가속기 + 부채 상환 (app-flow 토대)**
- ✅ DevUI(ImGui, `PASTEL_DEV_BUILD` 게이트) + Dev 빌드 구성 + GPU timestamp 프로파일링 — 비주얼 튜닝의 전제조건. **완료**
- ✅ `FrameRenderData` 스냅샷 — `drawFrame` 인자 10개 → 구조체 1개(`VulkanContext.h`). 렌더러는 public 경계에서 스냅샷만 소비. **완료**
- ✅ `GpuBuffer` RAII 래퍼 — `VkBuffer+VkDeviceMemory`(+mapped) move-only RAII로 통합, `createBuffer` 반환형화. `operator VkBuffer()`로 읽기 무변경. **완료**
- App-state 머신(MainMenu/Settings/Loading/Gameplay/Pause) + 입력 정책 + world session start/end. **MainMenu 클릭 UI + Settings 클릭 UI(+VSync 적용/AA 데이터) + Loading 1차 + Pause 클릭 메뉴 완료**: 시작 시 MainMenu 표시, `START` / `SETTINGS` row 클릭으로 Gameplay 시작 또는 Settings 진입(키보드 백업 없이 클릭 전용), 설정 row 클릭으로 VSync ON/OFF(swapchain present mode 재생성 적용)·AA OFF/FXAA/SMAA(UI/데이터) 변경, FXAA 실제 post AA 적용 완료, SMAA 1x 1차 적용 완료, 시작 시 Loading을 한 프레임 표시한 뒤 save 로드 + 초기 청크 로드 후 Gameplay 진입, `ESC`로 Gameplay/Paused 토글, Pause에서는 `RESUME` / `SETTINGS` / `QUIT`(세션 정리 후 타이틀 복귀) row 클릭 제공. Settings는 진입 위치(MainMenu/Pause)에 따라 `BACK`/`ESC` 복귀 위치가 달라짐. 인벤토리가 열린 Gameplay에서는 `ESC`가 Pause보다 인벤토리 닫기를 우선. 메뉴/settings/loading/pause 중 게임 업데이트·카메라 회전·월드 입력 차단, 메뉴/settings/loading 중 청크 스트리밍·저장 차단. 해상도·볼륨 등 추가 옵션은 예정

**Tier 2 — 비주얼 정체성 (DevUI로 실시간 튜닝)**
- ✅ 카메라 follow 댐핑 — `Camera` 내부 `m_followTarget` 지수 보간 + Loading 후 `snapToTarget`으로 저장 위치 스냅. 플레이어 추적감 개선, 회전은 기존 즉시 반응 유지
- ✅ hemisphere/colored ambient — `chunk.frag`/`triangle.frag`에서 법선 방향 기반 warm/cool ambient tint 적용. 밤 ambient 바닥값은 0.10으로 낮춰 야간을 더 어둡게 조율
- height fog
- ✅ vegetation alpha card 1차 — 절차 grass texture + alpha test + shadow 제외 + 청크별 dirty gate. **완료**
- ✅ density field 기반 grass dressing 1차 — 균등 확률 대신 patch density + open grass bias + density 기반 offset/scale variation 적용. **완료**
- ✅ high-quality grass 1차 — 3-card clump + density/scale 상향 후, 레퍼런스 피드백을 반영해 낮고 촘촘한 blade-field cluster + 약한 wind/fade/tint 1차로 전환. **완료**
- 다음 비주얼 후보: grass density/색 보정 · ground grass texture/detail · material-lite(AO/roughness/normal 선별) · ground dressing 텍스처화 · water 전용 표현 · shadow quality options · height fog · SMAA T2x/S2x 또는 MSAA/alpha-to-coverage
- 비고: grading/split-tone·fog·shadow·AO는 **이미 구현** → 격차는 튜닝 + 위 추가뿐

**Tier 3 — 확장 (rule of 3 도달 시)**
- 오디오(ambient/발걸음/SFX, 가벼운 single-header 미들웨어) · 경량 `IRenderPass` · 콘텐츠 데이터화(JSON 등) · 상호작용 인터페이스(`IInteractable`) · 순수 로직 테스트(inventory/save/craft)

> ✅ 즉시 가능한 작은 완성도: **오브젝트 충돌** — `collidable`이 이미 `ObjectDef` 데이터로 존재 → `World::isCollidableAt` + `canOccupy` 한 줄로 배선 완료. 건축이 장식에서 기능으로.

### 렌더링 품질 로드맵 (Tier 2 세부)
- ✅ color grading / tone mapping (post 1패스: exposure/contrast/saturation/split-tone/vignette)
- ✅ 그림자 접지 튜닝 (피터패닝 — bias 축소 + cull 조정 완료)
- ✅ FXAA 실제 적용: post pass에서 화면 edge를 완화. 자체 게임 UI는 FXAA 이후에 그려 픽셀 폰트 선명도 유지
- ✅ SMAA 1x = 원본 `SMAA_PRESET_ULTRA`: `AreaTex/SearchTex` LUT 기반 `edge detection → blend weight(+diagonal) → neighborhood blending` 3-pass. 값은 `threshold=0.05`, `search=32`, `diag=16`, corner rounding 25. edge detection은 **perceptual(감마) 공간**에서 수행한다 — sRGB offscreen이 샘플 시 linear로 디코드되므로 `pow(.,1/2.2)`로 복원(밤 장면에서도 AA 유지). reprojection/T2x/S2x는 제외. AA를 grade/tonemap 뒤 최종 색 위에서 돌리는 구조 전환은 실제 HDR/톤매핑 도입 시로 보류
- ✅ terrain texture mapping 1차: 청크 정점에 면별 UV/layer를 추가하고, `sampler2DArray` binding 3의 9개 절차 albedo layer를 샘플한다. vertex color는 tint/스타일 보정 + AO로 유지
- ✅ object texture mapping 1차: object pipeline이 `ChunkVertex.uv/layer`를 받아 공유 material texture array를 샘플한다. tree/workbench/fence는 WOOD, canopy는 LEAVES, rock/stone fence는 STONE layer를 재사용하며, visual-only dressing mesh는 `layer < 0`로 vertex color-only 유지
- ✅ authored texture loading 토대: `third_party/stb/stb_image.h`, `assets/textures` post-build copy, `createTextureFromFile(...)`를 추가했다. 현재는 `grass.png` 선택적 교체 + 절차 fallback까지만 연결했다
- ✅ authored terrain texture override 1차: `assets/textures/terrain/*.png`가 있으면 terrain texture array layer를 파일 이미지로 덮어쓴다. water는 전용 pass 전까지 절차 fallback 유지
- ✅ texture tone 안정화 1차: `chunk.frag`에서 raw texture 곱셈 대신 luma 기반 `materialDetail`을 만들어 vertex color 주 색감 + authored texture 표면 질감 구조로 정리
- ✅ layer별 texture strength 1차: grass/leaves는 낮게, dirt/farmland/stone은 높게, wood/wheat는 중간값으로 조절해 재질별 texture 존재감을 분리
- material-lite: full PBR 전환 전, albedo + tint + AO/roughness/specular 상수로 재질 차이를 표현. Normal map은 스타일과 충돌하지 않는 약한 강도로만 검토하고, displacement map은 실제 변위가 아니라 height mask/ground detail/density 보조 데이터 후보로 둔다
- shadow quality options: shadow map 해상도/PCF 샘플/거리 옵션, contact/blob shadow, 넓은 맵 이후 CSM 검토
- **그림자 안정화/품질 (우선순위↑)**: 구체 순서는 '알려진 이슈 / 렌더 품질 결함'의 개정 우선순위를 단일 출처로 따른다 — light frustum 축소(range↓, 부작용 없는 1순위) → shader slope-bias grazing 끝값 상향 → 원거리 부족 시 CSM(`shadowmappingcascade`) → PCF 커널 확대/PCSS(선택). 단일 2048² 맵 + 헐렁한 frustum이 현재 한계.
- **텍스처 필터링 품질 (우선순위↑)**: 밉맵 생성 + 이방성 필터링을 표준으로 도입한다(현재 둘 다 없음 — 원거리/grazing aliasing의 주원인). 해상도 상향(64²→512²+)은 그다음.
- terrain breakup은 타일별 vertex color 랜덤이 아니라 비격자 dressing layer로 처리(풀 clump, 잔돌, 흙/마른 풀 패치, 길 가장자리)
- height fog / vegetation variation / ground material detail / sky tint / LUT

### 다음 실행 순서 — 결정
다음 세션/작업은 아래 순서를 따른다. 큰 material/PBR 시스템이나 render graph로 먼저 가지 않는다.

1. ✅ 현재 SMAA 1x 1차 변경분을 커밋한다. **완료**
2. ✅ **TextureResource 기반**: grass texture 생성/upload 경로, SMAA LUT 업로드 경로를 `GpuBuffer`식 move-only RAII `TextureResource` + `createTexture(...)` 헬퍼로 통합. terrain texture array에서는 같은 수명 모델을 `createTextureArray(...)`로 확장. **완료**
3. ✅ **Terrain texture mapping 3a**: `sampler2DArray` 기반 terrain albedo layer, 청크 vertex UV/layer, descriptor binding 3, vertex color tint 유지. **완료**
4. ✅ **Terrain texture art 3b**: 새 의존성 없이 절차 terrain layer를 64×64 저대비 material mask로 튜닝. 물은 임시 placeholder이며 전용 water pass에서 재검토. **완료**
5. ✅ **Object texture mapping 3c**: object vertex input/mesh UV/layer를 배선하고, terrain material layer(WOOD/LEAVES/STONE)를 StaticProp에 재사용. **완료**
6. ✅ **Authored texture loading 4a**: `stb_image` RGBA8 로더, `assets/textures` 복사 규칙, `createTextureFromFile(...)`, grass texture 파일 fallback 토대. **완료**
7. ✅ **Authored terrain texture override 4b**: terrain layer별 Color texture 파일 override, 누락/크기 불일치 layer는 절차 fallback. water는 전용 pass 전까지 fallback. **완료**
8. ✅ **Texture tone 4c**: `fragColor * rawTexture`를 luma/chroma 기반 `materialDetail`로 안정화해 texture는 질감, vertex color는 주 색감 역할을 유지. **완료**
9. ✅ **Layer별 texture strength 4d**: grass/leaves/dirt/stone/wood/farmland 등 material layer별 texture 영향도를 shader에서 분리. **완료**
10. ✅ **High-quality grass 5a**: 3-card clump, grass 배치 확률/scale 상향, DevUI scene timing 확인. **완료**
11. ✅ **Reference grass blade-field 5b**: 큰 clump 반복감을 줄이기 위해 낮고 촘촘한 blade-field card cluster로 전환하고, 시간 기반 약한 wind sway, 거리 fade, tint, dense patch 다중 cluster를 적용. **완료**
12. **Ground texture / material-lite**: grass 바닥 Color texture를 먼저 적용하고, AO/roughness는 material-lite에서 약하게 사용한다. Normal은 스타일 충돌 여부를 본 뒤 낮은 강도로 검토하고, displacement는 직접 변위보다 height/density 보조로 우선 검토한다.
13. **Grass 후속 튜닝**: density/색/거리 fade/LOD, card texture variant는 스크린샷 피드백과 DevUI timing을 보며 좁게 조정한다.
14. ✅ **SMAA 품질 확장**: diagonal detection 포팅 + Ultra 프리셋(diag 16) 도달, edge detection을 perceptual 감마 공간으로 전환해 밤 AA 수정. **완료.** 남은 축(T2x/S2x·MSAA·grade/tonemap→AA 구조 전환)은 HDR/톤매핑 도입 시 별도 검토.

이 순서는 "품질을 올리되, 매 단계가 화면에 바로 기여하고 기존 구조와 자연스럽게 맞물리는" 경로다.

### Vegetation Alpha Card — **방향**
- 참고 이미지 수준의 자연스러운 풀밭은 단순 삼각형 기하 clump보다 alpha card 방식이 맞다. 기하 blade는 멀리서 삐쭉한 바늘처럼 보이기 쉽다.
- 목표: 풀 텍스처 1장 + 낮은 blade-field card cluster + instancing + 좌표 기반 결정론 배치. GRASS 전체 균등 배치가 아니라 숲 가장자리/물가/빈 잔디 영역 등 density rule로 조절.
- 성능 기준: GTX 1050 Ti 최소 60fps 안에서 grass는 핵심 비주얼 투자처다. cluster당 card 수, density, texture 품질, wind, LOD/fade를 DevUI/GPU timing으로 보며 적극적으로 올린다.
- 이후 확장: DevUI density/거리/scale 튜닝, card/texture variant, 거리 LOD 또는 원거리 density fade. 단순히 큰 clump를 키우는 방향은 반복 오브젝트처럼 읽히므로 피하고, 작은 blade가 바닥과 섞여 풀밭 면으로 읽히는 방향을 유지한다.
- 현재 상태: 외부 grass blade Color/Opacity 텍스처 로딩, alpha-test grass pipeline, 낮은 blade-field cluster mesh, 청크별 instance buffer, 좌표 기반 density field, shader 기반 tint/wind/fade, dense patch 다중 cluster 1차까지 연결 완료. 가까운 grass card를 shadow map에 넣는 alpha-tested shadow pass도 실험적으로 추가했지만, 현재 화면에서는 길쭉한 얼룩처럼 읽혀 어색하다. 다음 세션에서 높이/알파/거리/확률을 강하게 제한하거나 제거 여부를 판단한다. ground dressing은 저장하지 않는 좌표 기반 placement layer로 유효하지만, geometry placeholder는 과했기 때문에 cleanup에서 크게 축소했다. 다음 개선은 grass density/색 보정, 낮은 대비의 ground grass texture/detail, material-lite(AO/roughness/normal 선별), ground dressing 텍스처화 방향.

### 2.5D 고품질 잔디·룩 레시피 — **결정** (AAA 2.5D 관행 교차검증, 2026-06-03)
> 고정 시점 2.5D는 카메라가 잔디를 옆/바닥에서 들여다보지 않으므로, "잔디 한 포기당 삼각형 수"보다 **조명·색·접지 표현**이 체감 품질을 좌우한다. 덕코프류 룩의 핵심은 초고사양 메시가 아니라 *일관된 아트 + 제한된 카메라용 최적화된 표현*이다.

- **잔디 셰이딩 스택(우선순위순)**: ① 개체별 색 랜덤(반복감 제거) → ② height gradient(밑동 짙은 녹색 → 끝 밝은/노란 녹색) → ③ base/접지 AO(밑동 어둡게) → ④ **translucency/backlight(역광 시 노랗게 발광 — "진짜 잔디" 인상의 최대 요소, 저비용)** → ⑤ wind(현재 sin sway, 후에 wind noise map) → ⑥ 거리 fade/density falloff. 현재 ①②⑤⑥ 일부 + ③ 부분(`fragRootShade`) 구현, **④ 미구현(최우선 후보)**.
- **그림자 정책 — 결정**: 캐릭터/나무/오브젝트/건물 = 실시간 shadow map(필요 시 CSM + soft). **잔디는 그림자를 *받기만* 하고 개별 잔디는 *쏘지 않는다*** — 얇은 날 그림자는 단일 shadow map에서 ~1텍셀 폭이라 sun sweep에 깜빡이고 비용 대비 효과가 낮다(AAA 2.5D 표준). 대신 **ground contact AO / 어두운 패치**로 잔디를 바닥에 앉힌다. 작은 소품은 contact/blob shadow. (실험적 `shadow_grass` 캐스터는 이 정책에 따라 비활성화 — cleanup으로 완전 제거 예정.)
- **땅이 좋아 보이는 진짜 이유 = terrain blending**: 잔디 자체보다 흙/잔디/길/어두운 접촉부가 자연스럽게 섞이는 것. terrain albedo + noise color variation + dirt/grass blend mask + ground AO.
- **스코프(우리 규모)**: GPU-driven(compute frustum/occlusion cull + `vkCmdDrawIndexedIndirect` + mesh shader 절차 생성)은 100만~1000만 포기 오픈월드용이다. 우리는 2.5D + fog 57유닛 제한이라 청크별 instancing + 단순 cull로 충분 → **규모상 보류**(성능 미달이라서가 아님). 실제 draw 병목이 측정되면 indirect/compute cull로 승격.
- **라이팅 톤**: 따뜻한 태양광 + 차가운(푸른 회색/녹색) 그림자 + 그림자도 완전 검지 않음 — 현재 `max(shadow,0.4)` + hemisphere ambient와 방향 일치.

### Grid 규칙 vs Organic 표현 — **결정**
- 농사·설치/철거·충돌·저장 좌표는 grid 기반으로 유지한다. 플레이어 규칙은 예측 가능해야 한다.
- 자연 환경은 grid를 그대로 드러내지 않는다. 큰 잔디/흙 면을 타일별 색 랜덤으로 흔들면 격자감이 더 강해지므로 금지.
- 자연스러운 breakup은 렌더/월드의 별도 dressing layer로 만든다. 후보: alpha card 풀 clump, 잔돌, 흙/마른 풀 패치, 길 가장자리, 덤불/꽃/forage.
- dressing layer는 좌표 기반 결정론을 유지하고, 가능하면 저장 대상이 아닌 재생 가능한 시각 레이어로 시작한다.

### 스타듀식 오브젝트 경제 — **결정**(우선순위 ↑, 복셀 블록 편집은 은퇴)
순서: ✅① 인벤토리/작물 경제 → ✅② 제네릭 오브젝트 시스템 → ✅③ 자원 채집 → ✅④ 지형 불변화 → ⑤⑥ 제작·건축(아래 분할) → ⑦ 이후.
- **제작·건축 분할(2단계 의존성 순서):** ✅⑤a 인벤 제작(기본 레시피, 클릭형) → ✅⑥ 오브젝트 설치/철거(작업대·울타리를 월드에) → ✅⑤b 작업대 근처 고급 레시피(`requiresWorkbench`) 해금. **(아크 ①~⑥ 전체 완료 ✅)**
> ① 완료: 스택+개수 인벤토리, 숫자 렌더러, 낫 수확 + 드롭/줍기 레이어(`DroppedItem`). ② 완료: `m_objectMeshes` 메시 레지스트리 + 청크별 타입 그룹 + `ObjectDef` 테이블. ③ 완료: `tryHarvestObject`(도끼→나무/곡괭이→돌 → 드롭 → 줍기). ④ 완료: 복셀 설치/파괴 제거(지형 불변). ⑤a 완료: `Recipe` 테이블 + 클릭형 인벤 제작. ⑥ 완료: 오브젝트 설치/철거 + save v2 영속성. ⑤b 완료: `isObjectTypeNear` 작업대 근접 판정 → 고급 레시피 해금(돌담).
- **인벤토리**: 슬롯+개수(스택) 모델 + add/remove API + 숫자 렌더러(비트맵 digit quad, 텍스처 없음). 모든 드롭/소모가 여기로.
- `ObjectType → MeshRegistry` + `ObjectDefinition` 데이터 주도(mesh / collidable / castShadow / **harvestTool / dropItem / placeable**). 현재 tree/rock/workbench/fence/stone fence를 generic **StaticProp** 경로로 처리.
- 채집: 도끼→나무, 곡괭이→돌 = 오브젝트 레이캐스트 → 제거 + 드롭 아이템 → 인벤토리.
- **지형 불변**: 좌클릭 복셀 파괴 제거, 건축은 제작 오브젝트 설치/철거(플레이어 설치물만).
- variant 시스템(울타리 등 연결 구조)은 그 이후.

### 월드 모델 — **결정: 고정맵 + 절차 레이어**
- 핵심 통찰: **chunk = 스트리밍 단위(생성 방식이 아님)** → 절차/고정 둘 다 가능
- `WorldSource` 추상화 (ProceduralWorldSource / FixedWorldSource), 청크 시스템은 유지
- 3계층: **Permanent**(hand-authored 지형/마을/랜드마크) + **Persistent**(플레이어 변경, save) + **Renewable**(자원 재생 레이어)
- 장기 4-layer world: `Terrain / TileState / StaticProp / Crop / Entity`
- 현재 구현은 **절차생성 유지**. 고정맵 전환은 맵 데이터 포맷·콘텐츠 준비 후 단계적으로.

### 멀티플레이 — **방향: post-prototype v2** (지금 구현 안 함, 구조만 안 망침)
- **타이밍**: 싱글 프로토타입(농사·채집·건축·save·엔티티)이 설계상 굳은 직후, **콘텐츠가 불어나기 전**. 너무 늦으면 거대 코드베이스에 넷코드 retrofit이 더 비쌈.
- **장르 이점**: 코지 협동(소수 인원, 비경쟁) → 롤백/렉보상/예측 넷코드 불필요. 서버(또는 호스트) 권위 + 클라 입력/액션 전송 + 원격 플레이어 보간이면 충분(스타듀식).
- **이미 유리한 자산**: ① 시뮬(`World`/`GameState`) ↔ 렌더(`VulkanContext`) 분리, ② **결정론 지형**(시드만 공유하면 클라가 각자 생성 — 지형 전송 불필요), ③ `modified` 청크 추적 = 서버 델타와 매핑, ④ 클릭→액션이 이미 이산 명령에 가까움.
- **새로 필요**: 전송 계층(ENet/UDP) + 직렬화 / 서버 권위 상태 / 엔티티 복제(네트워크 ID·보간) / 시간·날짜 동기화 / 소유권 규칙.
- **지금부터 지킬 제약 3가지(비용 0, 이미 준수 중)**: ① 랜덤은 시드·좌표 기반 결정론 유지, ② 모든 상태 변경은 `World`/`GameState` 경유(렌더러에 권위 상태 두지 않기), ③ 게임 동작을 직렬화 가능한 이산 명령으로 모델링.
- **규모감**: 협동 멀티 = 수 주~수 개월 독립 서브시스템(지금까지 작업 전체와 맞먹는 별도 챕터). 렌더링은 거의 불변, 일은 로직 재배선·동기화·테스트에 집중.

### 보류 (실제 필요가 생긴 뒤)
- 청크 메모리 풀링(VMA/서브할로케이터) — 할당 개수 한계 대비
- 청크 메시 DEVICE_LOCAL — 현재도 staging path가 존재하나, 동적 청크 메시/식생 규모가 커지고 GPU vertex fetch 병목이 보이면 우선순위 상승

### 명시적 비목표 (Anti-goals) — rule of 3 / 실제 병목 전엔 **안 함**
개인 Vulkan 엔진이 "AAA 체크리스트"에 빠져 게임을 못 내는 함정 방어선. **단, 이것은 저품질을 목표로 하자는 뜻이 절대 아니다 — anti-goal은 "아키텍처/스코프 비대화"를 막는 것이지 "렌더링 품질"을 낮추라는 게 아니다.** 품질에 직접 기여하는 텍스처(밉맵·이방성·고해상도 authored), AA, shadow 품질(고해상도·CSM·soft), material-lite는 **적극 도입**한다. CSM·밉맵·이방성은 anti-goal이 아니다.
- **ECS 전면 전환** — 오브젝트가 sparse, OOP로 충분. (필요 시 hybrid SoA만 국소 적용)
- **Render Graph / FrameGraph** — 풀 그래프 X. 경량 `IRenderPass`까지만.
- **Asset DB / Material 노드그래프** — 에셋 수가 늘기 전엔 보류. 단, TextureResource/helper와 material-lite는 반복이 생기는 즉시 도입 가능.
- **Job/Async 시스템** — 청크 리빌드는 `MAX_CHUNK_BUILDS_PER_FRAME` throttle로 이미 완화. 식생 대량화 시점에.
- **VulkanContext 빅뱅 분할** — 한 번에 쪼개지 말 것. 작은 것부터(GpuBuffer→FrameRenderData→파이프라인 생성 점진 추출).
- **RTX/GI · mesh shader · bindless 대규모 시스템 · full PBR 전면 전환** — 지금 당장 필요하지 않다. PBR은 에셋/재질 수요가 실제로 생긴 뒤 material-lite에서 단계적으로 판단.
> 기준: **rule of 3**(세 번 반복되면 추상화) + **전면 rewrite 금지, 국소 리팩토링만**. (`CLAUDE.md` 단순함·수술적 변경과 일치)

### 불변식 (Invariants) — 깨지 말 것
- **결정론 생성**: 지형/배치는 해시·좌표 기반(`TerrainGen`). 랜덤은 항상 시드/좌표 기반.
- **레이어 분리**: `World`(데이터/시뮬) ↔ `GameState`(규칙) ↔ `VulkanContext`(렌더, **스냅샷만 소비**). 렌더러에 권위 상태 두지 않기.
- **상태 변경은 `World`/`GameState` 경유** (멀티·세이브 친화).
- **지형 불변** (복셀 편집 은퇴 — resolved).
- **세이브 버전 정책**: 개발 중엔 자유롭게 깸(버전 불일치=새 월드). pre-release 후엔 마이그레이션 추가.

---

## 알려진 이슈 / 메모

- **grass/ground dressing**: 외부 foliage Color/Opacity 텍스처 + alpha card + density field + 낮은 blade-field cluster + shader 기반 tint/wind/fade 1차는 완료. grass shadow 캐스터는 **비활성화 결정** — 개별 잔디 cast shadow는 단일 shadow map에서 ~1텍셀 폭이라 sun sweep에 깜빡이고 2.5D에선 비표준이라, 그라운딩은 **ground contact AO + base 어둡게**로 대체한다('2.5D 고품질 잔디·룩 레시피' 참조). 캐스터 파이프라인/셰이더 완전 제거는 cleanup 예정. ground dressing 1차는 구조 검증에는 성공했고, 과했던 갈색 patch/pebble placeholder는 cleanup에서 크게 축소했다. 다음 핵심은 바닥 grass texture/detail, material-lite 맵 선별 사용, ground dressing 텍스처화다.
- **작물**: 현재 voxel 타일(`WHEAT` + `TileState`)로 처리. 장기적으로 별도 `Crop` 인스턴스 레이어 분리 검토.
- 그림자 최소 밝기 `max(shadow, 0.4)` 는 파스텔 톤 유지를 위한 **의도된 스타일**(버그 아님).

### 렌더 품질 결함 (2026-06-03 코드 분석, LearnOpenGL shadow/texture 개념으로 교차검증)
> 짚인 체감 문제들의 근본 원인은 아트가 아니라 업계 표준 기법 누락이다. 구조는 좋아 국소 추가로 해결 가능(전면 재작성 아님). 진단은 LearnOpenGL의 shadow acne/peter-panning/blocky/frustum-fit/PCF/CSM, texture mipmap/anisotropic 개념과 일치함을 확인했다.

- **그림자 "빤짝임/일렁임" (blocky shadow aliasing)**: 주원인은 **그림자 맵 실효 해상도 부족**이다. 라이트 직교 박스 half-extent `range=80`(`VulkanContext_Frame.cpp`) → 160유닛을 2048텍셀에 펴서 텍셀당 ~0.078유닛인데, fog가 57유닛에서 가려 그 너머는 보이지도 않는다(해상도 절반 이상 낭비). 저해상도 계단(blocky) 에지가 태양이 천천히 회전하며 기어다녀 "불타듯 반짝"인다. **표준 해법: light frustum을 가시 범위에 맞게 축소(range↓) → 실효 해상도↑** (LearnOpenGL "blocky shadows: frustum을 scene에 딱 맞게"). texel snapping은 **적용 완료** — 플레이어 이동에 의한 translation 떨림 방지용 표준 stabilization이나, 회전 기반 blocky aliasing은 못 잡으므로 주 해법이 아니다.
- **일출/일몰 줄무늬 = Shadow Acne (Moiré)**: 그림자지지 않아야 할 면에 생기는 잘못된 self-shadowing. `chunk.frag`의 bias `mix(0.0015, 0.0003, NdotL)`는 slope-scaled bias **모양(수직→작게, grazing→크게)이 LearnOpenGL 권장과 정확히 일치**한다 — "bias가 꺼져 있다"가 아니라 **grazing 끝값(0.0015)이 약하고 저해상도와 겹쳐** 일출/일몰에 acne가 남는 것. **해법 순서: ① frustum 축소(해상도↑ — acne를 peter-panning 부작용 없이 줄임) → ② 그래도 남으면 shader bias의 grazing 끝값 소폭 상향(peter-panning 주시).** front-face culling은 청크가 hidden-face-culled = 단면 메시라 적용 불가(LearnOpenGL p18 "단면 object엔 불가" 단서와 일치). 하드웨어 caster depthBias(현재 청크 0/0, 오브젝트는 1.5/1.2)는 선택적 보조 레버.
- **텍스처 필터링 부재**: 전 텍스처 경로가 `mipLevels=1`, `mipmapMode=NEAREST`, 이방성 필터링 미사용 → 원거리 minification aliasing("자글자글"). 표준: **밉맵 + trilinear(`LINEAR_MIPMAP_LINEAR`) + anisotropic**(아이소메트릭은 항상 비스듬한 시점이라 aniso 효과 큼). 단 64×64 절차 텍스처는 magnification 흐림/계단이라 필터링으론 못 고치고 **원본 해상도 상향(512²+)**이 필요(LearnOpenGL #4 "텍스처가 애초에 작으면"). GTX 1660 Super 예산 충분.
- **색공간(sRGB) 누락 (2026-06-04 리뷰 검증)**: authored albedo 텍스처(grass color, terrain layer array)를 `VK_FORMAT_R8G8B8A8_UNORM`으로 업로드(`createGrassTexture`/`createTerrainTextureArray`, 1327행 등) → 조명 계산이 sRGB→linear 변환 없이 수행돼 색이 틀어진다. 표준: **albedo/color는 `*_SRGB` 포맷**(샘플 시 하드웨어가 linear로 디코드), opacity/AO/roughness 등 **mask는 UNORM** 유지. swapchain은 이미 `B8G8R8A8_SRGB`.
- **HDR 오프스크린 타깃 없음**: scene을 swapchain의 8-bit sRGB 계열로 바로 렌더 → 톤/하이라이트 여유가 작다. bloom/grading/SSAO를 제대로 하려면 **`R16G16B16A16_SFLOAT` linear HDR 타깃** 후 톤매핑이 표준(post 확장 시 도입).
- **시작 시 흰 화면(5~10초)**: world/청크 로드는 이미 Loading 상태에서 처리된다. 실제 원인은 **`VulkanContext` 생성자의 동기 초기화**(파이프라인 약 15개 드라이버 컴파일 + 텍스처 업로드)가 창이 이미 보이는 상태에서 첫 present 전까지 블록하기 때문이다. 메인 메뉴조차 이 init 뒤에 그려지므로 "텍스처 로딩을 START 후로 이동"으로는 해결되지 않는다. 완화책: 첫 프레임 전까지 창 숨김(`GLFW_VISIBLE=false`→첫 present 후 `glfwShowWindow`), **`VkPipelineCache`** 도입(2회차부터 컴파일 단축), 필요 시 스플래시 1프레임.
### 게임 견고성·데이터 무결성 (코드리뷰 검증, 2026-06-04)
> 외부 LLM(Codex) 리뷰를 코드로 직접 검증. 렌더 품질보다 **실제 게임을 깨뜨릴 수 있는** 데이터/진행도 문제가 더 시급하다고 판단(AGENTS.md §9: 리뷰는 권위가 아니라 검증 대상).

- ✅ **Phase 0 완료 (2026-06-04)**: ① 인벤토리·드롭·watered 세이브(v3) · ② atomic write + 로드 범위/enum 검증(실패 시 무변경) · ③ dt clamp(0.1s) · ④ 작물 catch-up(`growthTick`이 unloaded 청크도 틱). **남은 견고성**: Vulkan 반환값 검사·생성자 RAII·순수 로직 테스트.

- **P1 인벤토리·드롭 미저장**: `World::save()`가 플레이어 위치·시간·수정 청크(타일/growthStage/lastUpdatedDay/오브젝트)만 저장. `GameState`의 인벤토리·드롭·watered는 빠져 재시작 시 제작·채집·씨앗 진행이 손실 → 세이브 포맷에 인벤토리/드롭 추가(버전 업).
- **P1 세이브 비원자적 + 로드 검증 없음**: `save.dat`에 직접 기록(저장 중 크래시 시 기존 세이브 파손) → 임시파일 write→flush→rename 표준 적용. `load()`가 `count`/`objCount`/`TileType`/`ObjectType` 범위를 검증하지 않아 손상 세이브가 `objectDef`/`m_objectMeshes` OOB로 이어질 수 있음 → 범위 검증 + 실패 시 부분 적용 롤백.
- **P2 작물 성장 catch-up 부재**: `growthTick()`이 로드된 `m_chunks`만 순회 → 멀리 떠난 동안 `m_modifiedUnloaded` 작물은 시간이 지나도 안 자람. `lastUpdatedDay` 저장값을 catch-up에 안 씀 → day-delta 기반 catch-up.
- **P2 dt clamp 없음 / Vulkan 반환값 다수 미검사 / 생성자 RAII 미흡**: 큰 dt(resize·stall·디버거 정지)가 시간·성장·이동을 튀게 함 → `dt` clamp. `vkQueueSubmit`/`vkBeginCommandBuffer`/`vkMapMemory` 실패 무시, 생성자 중간 throw 시 raw 핸들 누수 → 점진 하드닝.
- **P3 picking z=0 평면 가정**: 레이가 z=0 평면 교차 후 컬럼 스캔. 언덕/오브젝트 조준 시 XY가 어긋날 수 있으나 **±1 인접 클램프가 가려 실害 작음**(Codex 과장). 정밀 필요 시 voxel DDA.
- **테스트 부재**: save/load round-trip, inventory/craft, growthTick, placement 순수 로직 테스트 없음 → 세이브 변경과 함께 도입 검토(렌더보다 회귀 위험 큼).
- **보류(측정 후)**: 청크/식생/오브젝트 버퍼 DEVICE_LOCAL화 — 현재 GPU ~2ms, vertex-fetch 병목 미측정. HOST_VISIBLE 매핑은 동적 데이터엔 정당. 병목이 측정되면 staging→DEVICE_LOCAL 승격(Codex는 P1이라 했으나 규모상 보류).

### 통합 수정 우선순위 (재배치, 2026-06-04)
렌더 품질 + 견고성을 합쳐 "위험·비용 대비 효과" 순으로 재배치.
- ✅ **그림자 1차(완료)**: light frustum 축소(range 80→45) + texel snapping + soft PCF 5×5 + grass shadow 캐스터 비활성화.
- ✅ **Phase 0 — 데이터 무결성/견고성 (완료, 2026-06-04)**: ① 인벤토리·드롭(+watered) 세이브(v3) · ② atomic write + 로드 검증 · ③ dt clamp · ④ 작물 catch-up. (순수 로직 테스트 + Vk 반환값/RAII 하드닝은 후속)
- **Phase 1 — 렌더 correctness 토대**: ⑤ albedo `*_SRGB` / mask UNORM 분리 → ⑥ 밉맵 + trilinear + anisotropic(+`samplerAnisotropy` feature·device 적합성 체크). (⑦ HDR float 오프스크린은 bloom/grading 확장 시)
- **Phase 2 — 비주얼 폴리시 (덕코프)**: ⑧ 잔디 translucency/backlight + ground contact AO(+비활성 grass shadow 리소스 제거) → ⑨ 그림자 해상도 4096/동적 texel size/CSM·소품 contact shadow.
- **Phase 3 — 콘텐츠/확장**: ⑩ worldgen 풍부화(언덕 채움·height/slope)·water material/pass → ⑪ MSAA+alpha-to-coverage(잔디 alpha edge) → ⑫ 흰 화면(창 지연+pipeline cache) → ⑬ authored asset/font.
> shader slope-bias grazing은 해상도/PCF로 충분하면 생략. CSM·헬퍼 추출·grass shadow 완전 제거 cleanup은 위 Phase 안에서.
