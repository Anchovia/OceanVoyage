# OceanVoyage 개발 로드맵

이 문서는 OceanVoyage를 "렌더만 바다인 농장 코드베이스"에서 "조작도 게임도 바다인 해운·무역 RPG"로 전환하기 위한 **장기 개발 순서**를 한 곳에 정리한다.

- 게임 비전·기둥은 `DESIGN.md`, 코드 경계·구조는 `docs/ARCHITECTURE.md`, 농장 코드 제거 계획은 `docs/MIGRATION_PLAN.md`, 엔진/렌더 기술 과제는 `docs/ENGINE_TODO.md`, 변경 이력은 `DEVLOG.md`를 따른다.
- 이 문서는 그 위에서 **"무엇을, 어떤 순서로, 어디를 건드려, 무엇을 확인하며"** 진행하는지를 다룬다. 세부 구현 결정은 각 Phase 진입 시점에 해당 문서에서 확정한다.
- 갱신 시점: 개발 순서·범위가 바뀔 때. 각 Phase가 끝나면 상태와 검증 결과를 갱신한다.

> **제1원칙(불변):** RTX 3060급 / 1080p~1440p / 60fps에서 AAA/AA급 사실적 해양. 저사양 편법·화면 도색 wake/foam·노이즈 블롭을 최종 표현으로 넣지 않는다(`CLAUDE.md` §0, `DESIGN.md` Visual North Star). 로드맵의 어떤 항목도 이 기준을 낮추지 않는다.

상태 범례: ✅ 완료 · 🔶 진행/부분 · 🔴 예정

---

## 0. 로드맵 개요

| Phase | 주제 | 상태 | 주 대응 문서 |
|---|---|---|---|
| **0** | 기준점 고정 (현재 빌드 회귀 확인) | 🔶 | `RUN_CHECKLIST.md` |
| **1** | 기본 선박 상태와 항해 물리 | 🔶 1차 완료 | `MIGRATION_PLAN.md` Phase 3 |
| **2** | 농장 구조 제거 + 렌더 데이터 경계 | 🔶 핵심 완료 | `MIGRATION_PLAN.md` Phase 4 |
| **3** | VoyageSave + 항구/화물/교역 1차 | 🔴 | `MIGRATION_PLAN.md` Phase 5 |
| **4** | 렌더링 후속 (TAA·리드백·반사·상수) | 🔴 | `ENGINE_TODO.md` P1/P2 |
| **5** | 세계 표현: 항구·섬·풍향·항로 | 🔴 | `ARCHITECTURE.md` (OceanWorld) |
| **6** | 경제·선박 성장·회사 진행 | 🔴 | `DESIGN.md` |
| **7** | 항해 심화: 풍향·돛·날씨·해상 위험 | 🔴 | `DESIGN.md` |
| **8** | 비주얼 고도화 + 에셋 파이프라인 | 🔴 | `DESIGN.md` Visual / `ENGINE_TODO.md` |
| **9** | 기술 부채 정리·엔진 구조 안정화 | 🔴 | `ENGINE_TODO.md` P3 / `ARCHITECTURE.md` |
| **10** | 게임 완성도: UX·튜토리얼·진행 목표 | 🔴 | `DESIGN.md` |

현재 위치: 렌더링 기준점(화질) 달성, **Phase 1 항해 물리 1차 완료**(2026-06-09), **Phase 2 렌더러-World 분리 핵심 완료**(2026-06-09) — 렌더러가 `World`/`TileType`/inventory를 모르고, `FrameRenderData`는 `camera`+`ship*`+app 상태만 받는다. 농장 HUD/상호작용/청크·dressing·오브젝트 렌더는 제거됐고, 죽은 셰이더·청크 TU도 CMake에서 정리됨. 공유 디스크립터의 죽은 grass/terrain 텍스처도 제거됨(2d-5b, 2026-06-10). **Phase 3a VoyageSave 완료**(2026-06-11, `"OVYG"` v1 — 항해 상태 저장/복원, 구 PFRM 거부). **농장 레거시 정리 완료**(2026-06-11): 죽은 세이브 배관 → `GameState` inventory/drops/craft → `World`/`Chunk`/`TerrainGen` 삭제 → `Player` 미러 shim 제거 4개 슬라이스로 종결, `src/world/`와 `Player.h`가 코드베이스에서 소멸. 다음 본작업은 **3b(항구·화물 데이터)** 이며, TAA/async 같은 렌더 후속(Phase 4)보다 먼저다.

---

## 진행 원칙

- **항해 물리가 먼저.** 화질은 기준점까지 왔으나 조작은 농장 `Player` 잔재다. 이걸 두고 렌더 후속부터 하면 화면만 좋아지고 게임 정체성 문제가 남는다.
- **항상 빌드 가능.** 각 Phase·각 작은 단위가 끝날 때 빌드되는 상태를 유지한다(`CLAUDE.md` §7, 빌드는 사용자 소유).
- **삭제보다 교체.** 농장 코드는 대체 구조(ShipState/렌더 스냅샷/VoyageSave)가 생긴 뒤 작은 커밋으로 제거한다.
- **placeholder는 명시.** 임시 표현은 placeholder로 표기하고 최종 품질 경로를 막지 않는 범위에서만 둔다.
- **async compute는 마지막.** 큐/동기화 변경 폭이 크다. 항해/렌더 경계가 안정되고 실제 병목을 측정한 뒤(Phase 9).

---

## Phase 0 — 기준점 고정

**목표:** 이후 버그가 새 작업 때문인지 기존 문제인지 분리하기 위해 현재 동작을 기록한다. (화질 기준점은 이미 달성, 게임 동작 기록만 남음)

작업:
- [ ] 사용자 빌드 실행 → 메뉴 진입, 게임 시작, 바다 FFT, 선박, wake, 저장/로드 무크래시 확인 (`RUN_CHECKLIST.md` §3·§4·§4.1·§6)
- [ ] 현재 이동 동작 기록: W/S/A/D가 관성 없이 즉시 방향 전환하는 농장 컨트롤러 잔재임을 기준점으로 명시
- [ ] 결과를 `RUN_CHECKLIST.md` 점검 기록에 남김

수정 파일: 없음. **완료 기준:** 현재 상태가 "작업 전 기준점"으로 문서화됨.

---

## Phase 1 — 기본 선박 상태와 항해 물리 (🔶 1차 완료)

> **상태(2026-06-09):** 1a~1c + 1d의 물리/미러링 완료, 빌드·체감 검증됨(`src/game/GameState.{h,cpp}`만 수정). `ShipState`가 `GameState`에 들어갔고, WASD→throttle/rudder, 관성·선회 물리가 동작한다. `Player`는 임시 shim으로 ship을 미러링해 카메라·wake·부력·그림자·세이브가 무수정으로 따라간다. **남은 것**(Phase 2와 함께): `FrameRenderData`의 `player*`→`ship*` 이름 정리, `Player` 제거. 선회 polarity·감속감 등 상수 튜닝은 체감 보며 조정.

**목표:** `Player` 위치로 선박을 흉내 내는 구조를 끝내고, 관성·선회반경을 가진 진짜 `ShipState`로 항해한다. **이 Phase의 성공 = "배가 더 이상 농장 캐릭터처럼 걷지 않고 배처럼 움직인다".**

이 Phase에서 **하지 않는 것:** 항구·교역·저장 구조 교체·UI 전면 개편·TAA·async.

### 1a. ShipState 도입
- [ ] `ShipState { glm::vec2 position; glm::vec2 velocity; float heading; float yawRate; float throttle; float rudder; }` 추가
- [ ] `GameState`가 `ShipState` 소유 + `ship()` getter. 기존 `Player`는 아직 남김(호환)
- [ ] 초기 위치/heading을 기존 player 시작값(`{15,15}` 근처)에 맞춤 → 화면 변화 거의 없음
- 닿는 파일: `src/game/GameState.h/.cpp` (필요 시 `src/game/Ship.h`는 별도 승인 후)

### 1b. 입력 의미 교체
- [ ] W/S → throttle 증감, A/D → rudder 좌/우. Q/E 카메라 회전은 유지
- [ ] rudder는 `[-1,1]` clamp, 입력 없으면 0으로 부드럽게 복귀. throttle은 천천히 0 복귀(조작 확인 용이)
- [ ] 농장식 좌우 strafing 제거, heading을 이동 방향에 즉시 맞추던 코드 제거

### 1c. 항해 물리 1차
```text
forward   = {cos(heading), sin(heading)}
velocity += forward * throttle * thrust * dt      // 전진 추진
velocity -= velocity * linearDrag * dt            // 선형 저항
yawRate  += rudder * turnPower * speedFactor * dt  // 속도 있어야 타각이 먹음
yawRate  -= yawRate * yawDamping * dt              // 선회 감쇠
heading  += yawRate * dt
position += velocity * dt
```
- [ ] 전진 속도 상한, 후진은 더 낮게 제한. 미세 속도는 0으로 스냅(떨림 방지)
- [ ] 정지 상태에서 제자리 회전 거의 불가(speedFactor)
- [ ] dt clamp(0.1s, `main.cpp`)는 기존 유지, 항해 물리도 같은 dt 사용
- 초기 상수(튜닝 시작점): `maxForwardSpeed≈9`, `maxReverseSpeed≈2`, `thrust≈5.5`, `linearDrag≈0.35`, `turnPower≈1.2`, `yawDamping≈1.8`, `rudderReturn≈3.0`

### 1d. 기존 Player 이동 끊기 + 렌더 입력 이름 정리
- [ ] 선박 이동을 `World::isWalkable`/타일 충돌(`canOccupy`)로 막지 않음
- [ ] `FrameRenderData`/`drawFrame` 입력 이름을 `playerPosition/Velocity/Heading` → `shipPosition/Velocity/Heading`으로 교체
- [ ] wake 입력(`m_oceanWakeShip*`), 부력(`updateShipTransform`), 그림자 중심(CSM center), 카메라 타깃을 전부 `ShipState` 기준으로 통일
- 닿는 파일: `src/main.cpp`, `src/renderer/VulkanContext.h`, `VulkanContext_Frame.cpp`, `src/game/Camera.*`

**검증:**
- W 가속 → 떼면 관성 후 감속, wake가 뒤로 생성
- W+A/W+D는 곡선 선회(heading과 실제 진행 방향이 완전히 같지 않은 게 관성)
- 정지 중 A/D만으로 제자리 팽이처럼 돌지 않음
- 이동 중에도 선박이 파면에 계속 떠 있음(부력 정상), 선회 시 wake가 heading/velocity와 크게 어긋나지 않음

> 첫 작업 단위로 자른다면: **`ShipState` 추가 + WASD를 throttle/rudder로 해석 + 관성 이동 1차**. 여기까지가 OceanVoyage가 "조작도 배"가 되는 최소 묶음.

---

## Phase 2 — 농장 구조 제거 + 렌더 데이터 경계

**목표:** 렌더러가 게임 규칙을 모르게 하고, 화면에서 농장 UI를 걷어낸다. 큰 삭제는 대체 구조가 생긴 뒤 작은 커밋으로.

**상태(2026-06-09): 렌더러측 핵심 완료.** `VulkanContext`는 `World&`를 받지 않고(`VulkanContext(Window&)`), `World`/`TileType`/inventory 의존이 0이다. 아래 2a~2d는 ~10개 빌드 검증 슬라이스로 나눠 적용·검증 완료(2d-5b 죽은 grass/terrain 디스크립터 정리 2026-06-10 완료). 남은 것은 게임측 농장 레거시(Phase 3)다. 죽은 frustum은 2026-06-10 제거 완료.

### 2a. FrameRenderData 정리 ✅
- [x] 유지: camera, ship position/velocity/heading/throttle/rudder, gameTime, timeOfDay, menu/settings/loading/paused, vsync, aaMode
- [x] 제거: targetTile, hotbarSelected, inventory, inventoryOpen, drops, nearWorkbench, day
- [x] HUD용 ship 데이터(speed/heading/throttle/rudder)를 frame.ship*로 전달 — 렌더러가 `GameState`/inventory를 몰라도 HUD를 그림

### 2b. 농장 HUD → 선박 HUD ✅
- [x] hotbar/inventory/crafting/selector/drops 렌더 제거, I키 inventory 토글 비활성
- [x] 최소 선박 HUD: `SPD`, `HDG`, `THR`, `RUD` (기존 vector-font helper 재사용, 새 의존성 없음)
- [x] 메뉴/설정/로딩 UI 유지

### 2c. 농장 상호작용 제거 ✅
- [x] `targetTile`/selector, 마우스 tile picking, `nearWorkbench`, drops pickup, `canOccupy`를 gameplay update에서 제거
- [x] `GameState::update` 시그니처를 `update(dt, input)`로 축소(Camera/World 인자 제거), 농장 include 제거
- 비고: `craft`/`Recipe`/`ItemType`/`m_inventory`/`m_drops`는 아직 `GameState`에 남아 있음(현재 save가 사용) → Phase 3 VoyageSave에서 정리

### 2d. 렌더러-World 분리 ✅ (핵심)
- [x] 전수 물 타일 월드에선 청크·dressing·오브젝트 메시가 전부 빈 메시 → 시각 변화 0 확인 후 제거
- [x] 청크/grass/ground/pebble/object 렌더·파이프라인·메시·빌더 제거(2d-1~2d-4)
- [x] `VulkanContext` 생성자에서 `World&` 제거, `m_world` 멤버·`world/World.h` include 제거(2d-5a)
- [x] 죽은 셰이더 10종 + 빈 `VulkanContext_Chunk.cpp`를 CMake·디스크에서 제거(2d-5c)
- [x] **2d-5b**: 공유 scene/reflection 디스크립터에서 죽은 grass/terrain 텍스처(`m_terrainTex`/`createTerrainTextureArray`/`m_grassTex`/`m_grassOpacityTex`) + write 제거(2026-06-10). 레이아웃 binding 2/3/4 삭제 → `{0,1,5,6,7}` 비연속, ship은 5/6/7 유지(살아있는 셰이더 무수정). `createTextureArray` 범용 헬퍼는 보존. 죽은 `m_frustum`/`m_reflectionFrustum`은 후속 슬라이스로 제거 완료(2026-06-10, `Frustum.h`는 향후 컬링용 보존)

**검증:** 화면에서 농장 UI가 사라지고 항해 HUD가 보임. 저장/로드는 기존대로 무크래시 유지. 자세한 제거 순서·조건은 `MIGRATION_PLAN.md` Phase 4, `CODE_CLASSIFICATION.md` 참고.

---

## Phase 3 — VoyageSave + 항구/화물/교역 1차

**목표:** 농장 `PFRM` save에서 벗어나 선박 상태를 저장하고, 첫 교역 루프(항해 → 항구 → 매매 → 항해 → 이익)를 성립시킨다.

### 3a. VoyageSave 최소 스키마 ✅ (2026-06-11)
- [x] 저장 책임을 `World::save/load`에서 별도 save helper(`src/game/VoyageSave.{h,cpp}`)로 이동
- [x] 새 magic `"OVYG"` + version 1. v1 필드: gameTime, ship position/velocity/heading/yawRate/throttle/rudder
- [x] atomic write(`.tmp` → rename), magic/version/finite-float 검증, 전체 성공 시에만 commit(기존 World save의 장점 계승)
- [x] 구 `PFRM` save는 magic 불일치로 명시적 거부 → 새 게임
- 저장하지 않을 것: timeOfDay(gameTime에서 계산), wake field, FFT phase(gameTime으로 재현)
- 후속: 죽은 코드가 된 `World::save/load`·`GameState::setPlayerPosition/setInventory/setDrops`는 농장 레거시 정리 슬라이스에서 제거

### 3b. 항구·화물 데이터 ✅ (2026-06-11)
- [x] `Port { id; name; glm::vec2 position; radius }`, 시작 항구 1개(BRISTOL)
- [x] near port 감지(거리 기반) → HUD에 `NEAR PORT`/거리·8방위 표시(`PRT`)
- [x] `CargoGoodId`(Coal/IronOre/Steel/Machinery/Grain), `CargoStack`, `CargoHold { capacity; stacks; }`, HUD `CRG 0/100`
- [x] `int money`(시작 1000) + HUD `GLD`, save v2에 money/cargo 추가(version bump, 검증 포함)

### 3c. 항구 모드 + 매매 (🔶 3c-1 완료 2026-06-11)
- [x] game mode `Sailing`/`Docked` 분리(AppMode와 섞지 않음). 항구 반경 안 + 저속(≤2m/s)에서 Enter로 입항, 항구 메뉴(`SET SAIL`/`TRADE`)에서 출항
- [ ] `MarketEntry { good; buyPrice; sellPrice; stock; }`, 시장 UI(Up/Down 선택, B 구매, S 판매, Esc 나가기)
- [ ] buy/sell validation(money·capacity·stock) + apply, 저장/로드 후 유지
- [ ] 항구 2개 + 가격 차이 → 첫 교역 루프 검증(A 구매 → B 항해 → B 판매 → 이익), 항해 도중 저장/복원

**검증:** 재실행 후 선박 위치·방향·money·cargo 복원, 손상 save 무크래시, 구 PFRM 오독 없음. 첫 게임 루프 성립.

---

## Phase 4 — 렌더링 후속 (TAA·리드백·반사·상수)

**목표:** 항해/저장/항구 1차가 안정된 뒤, 화질·비용·유지보수성을 올린다. 세부 항목과 현황은 `docs/ENGINE_TODO.md`가 단일 출처이며, 여기서는 **순서**만 정한다. 각 기법의 논문·구현 레퍼런스는 `docs/RENDERING_REFERENCES.md`(TAA=§5, SSR/반사=§4, 해양=§2).

1. **기준 성능 측정** — 대표 장면 3개(낮/석양 grazing/밤), 1080p·1440p, AA·반사 토글별 GPU timing 기록
2. **TAA 도입** — Halton jitter + history reprojection + neighborhood clamp + water reactive 고려. SSR용 `prevViewProj`/`temporalParams` 배선 재사용. (`ENGINE_TODO.md` P1)
3. **SMAA 순서/색공간 정리** — tone-map/grade target 후 perceptual LDR에 AA 적용
4. **부력 리드백 축소** — 전체 512²×3 이미지 복사 제거 → sample point만 GPU compute로 추출하는 작은 버퍼 (`ENGINE_TODO.md` P2)
5. **반사 비용 정책** — reflection mode(SkyOnly/SSR/Planar/SSR+Planar) + 플래너 해상도/대상 제한 + SSR step 옵션
6. **셰이더 상수 단일 출처화** — `OCEAN_FFT_N`/`CASCADE_L`/`WAKE_*`/`SEA_LEVEL`/`SHADOW_MAP_SIZE` specialization constant or generated include
7. **ocean mesh/buffer 정리** — ocean vertex/index를 DEVICE_LOCAL+staging, projected-grid/clipmap은 별도 조사
8. **async compute** — 마지막. queue family/command pool/ownership/semaphore 설계 후, GPU capture로 실제 이득 확인하고 도입 (`ENGINE_TODO.md` P2)

---

## Phase 5 — 세계 표현: 항구·섬·풍향·항로

**목표:** "무한 바다 위 UI 항구"에서 벗어나 바다를 기억 가능한 공간으로 만든다.

- **OceanWorld 설계** — 농장 `World/Chunk/TileType`을 대체. 필드: ports / islands / wind / region seed / discovered. 렌더러를 모르고, mesh 인스턴스는 별도 변환 단계에서 생성(`OceanWorld → RenderSceneSnapshot`).
- **항구 확장** — 3개 이상(Trade/Industrial/Coal/Shipyard 타입), 항구별 시장 차별화, nearest-port 거리·bearing HUD(`PORT: Ironhaven 1.2km NE`).
- **항구 시각 1차** — buoy/lighthouse/dock/warehouse. **단순 컬러 큐브·flat marker 금지**, procedural mesh라도 부두/창고/등대 실루엣 수준. 농장 `ObjectType` 재사용 금지(새 port object path).
- **섬·해안선 1차** — ellipse island shape → 불규칙 shoreline. 농장 voxel chunk 재사용 금지. 섬 충돌은 ellipse distance(타일 충돌로 회귀 금지).
- **해안/얕은 물** — island distance field → shallow water tint + shoreline foam(거리+wave energy 기반, 단순 노이즈 띠 금지).
- **풍향 1차** — 전역 `windDirection/windSpeed`, 느린 변화, HUD 표시. 항해 물리에 sail assist factor로 약하게 연결(FFT spectrum 연동은 후순위).
- **항로/목적지** — selectedPortId, 거리·방향·도착 판정.

상세 데이터 구조(Port/Island/Wind/RouteTarget)는 진입 시 `ARCHITECTURE.md`에 확정.

---

## Phase 6 — 경제·선박 성장·회사 진행

**목표:** "싸게 사서 비싸게 판다"를 성장 루프로 바꾼다.

- **경제 데이터** — `TradeGoodDef { id; name; category; basePrice; unitWeight; }`(원자재/가공재/소비재 8~12종, 하드코딩 시작), 항구별 생산/소비 태그.
- **가격 1차** — `price = basePrice * demand / supply` + buy/sell spread + clamp(40~250%). NPC 무역·전역 수급 시뮬레이션은 후순위.
- **시장 재고/수요** — stock/demand + 주기적(in-game day) 회복으로 무한 매매·exploit 방지.
- **CargoHold 개선** — 단순 수량 → unitWeight 기반 적재량 제약, stack merge/split.
- **선박 스탯** — `ShipDef`(불변 스펙: cargoCapacity/maxSpeed/accel/turnRate/drag/draft) ↔ `ShipState`(현재 상태) 분리. 하드코딩 thrust/drag/turnPower를 `ShipDef`에서 가져옴.
- **업그레이드 + 조선소** — Hull/Cargo/Sail/Rudder 레벨(초기 3단계), Shipyard 항구에서 money로 구매, save에 레벨 저장.
- **money/평판 진행** — totalProfit, reputation(계약 완료·수익 milestone), unlock 조건.
- **계약/의뢰 1차** — `DeliveryContract { origin; destination; good; quantity; reward; }`, 일반 cargo 요구 방식.
- **유지비/연료** — 너무 일찍 넣지 않음. 항구 수수료 → day-based upkeep 순. 증기선 fuel(Coal 소비)은 데이터만 준비.
- **밸런스** — 첫 cargo upgrade 2~4회 거래, 첫 ship upgrade 5~8회 거래 목표. 같은 항구 buy/sell 반복 exploit smoke test.

---

## Phase 7 — 항해 심화: 풍향·돛·날씨·해상 위험

**목표:** 단순 운송이 아니라 "항해 게임"처럼 느껴지게 한다.

- **풍향 모델** — HUD용 wind를 실제 추진 계산으로 승격. deterministic time function, 분 단위 완만 변화, gust.
- **돛 추진** — 상대 풍각 → sail efficiency curve(정면 낮음, beam reach 높음). throttle을 sail trim으로 해석. 역풍 감속.
- **rudder/hull 개선** — 속도 의존 선회, lateral slip damping, yaw inertia. upgrade와 연결.
- **sail trim vs engine throttle** — 범선 `sailSetting` / 증기선 `engineThrottle` 분리, HUD `SAIL 72%` / `ENG 55%`.
- **no-go zone/tacking** — 정면 ±35~45° 추진 효율 급감, 지그재그 필요. 급정지 대신 완만 감속, `IN IRONS`/`POOR WIND` 경고.
- **날씨** — `Clear/Cloudy/Rain/Storm` 완만 전환, sky tint/fog/sun/visibility/wind modifier 연결. 비/스톰 시각은 placeholder 명시(screen-space streak → 발전).
- **시간대 항해** — 밤 시야 감소, 등대/항구 light 중요, ship lights.
- **해상 위험** — `hullIntegrity`(충돌/storm/grounding) + 조선소 수리, `HULL 87%` HUD.
- **보급(증기선 준비)** — Coal/fuel 우선, crew food/water는 회사/승무원 시스템 이후.
- **항해 보조 UI** — compass(heading/target bearing/wind), speed+wind efficiency, route ETA, 경고. 항해 중엔 작고 조용하게.

---

## Phase 8 — 비주얼 고도화 + 에셋 파이프라인

**목표:** "기능은 있는데 시각적으로 비어 있는 상태"를 줄인다. **싸구려 placeholder를 최종 표현처럼 두지 않는다.**

- **에셋 정책** — 직접 제작/CC0/임시/최종 구분, 라이선스 기록(`docs/ASSET_LICENSES.md` 또는 `assets/README.md` — 새 파일은 별도 승인). albedo sRGB / normal·roughness·spec·AO linear, mipmap+anisotropic.
- **선박 파이프라인 일반화** — 단일 LSV018 하드코딩을 `ShipModelResource`/`ShipMaterial`/`ShipHullProfile`로, ship def에 model/material path 연결.
- **선박 비주얼** — material 검증, wetline, hull profile 기반 shear foam 강화(흰 outline 금지), LOD.
- **항구 에셋** — pier/warehouse/lighthouse부터. material-lite라도 최종 placeholder로 명시. 항구 조명(등대 beacon, point light 수 제한, emissive).
- **섬/해안 고도화** — 불규칙 shoreline, sand/rock/grass material, shoreline foam mask/advection, 얕은 물 색. 농장 voxel/grass 재사용 금지.
- **물 고도화 2차** — wind→roughness/whitecap/wave detail, bow/storm spray particle, wake power(speed/beam/draft), foam temporal 안정화(TAA reactive mask 연결).
- **하늘/날씨 비주얼** — cloud layer, rain particle, lightning, moon/night, fog/horizon.
- **UI 비주얼 통일** — 항해 HUD/항구 UI를 산업혁명 해운 톤으로, 농장 UI 잔재(item color square, hotbar/crafting layout) 제거 확인.
- **RenderScene 확장** — ship/port/island/vegetation/ocean/sky/UI 종류 정리, material id, instance buffer, culling, debug draw(port radius/route/wind/collision).

---

## Phase 9 — 기술 부채 정리·엔진 구조 안정화

**목표:** 새 기능보다 "계속 쌓아도 무너지지 않는 구조". 세부는 `ENGINE_TODO.md` P3와 함께 본다.

- **VulkanContext 파일 분리** — 4000+줄 `VulkanContext_Init.cpp`를 기능별(Swapchain/Pipelines/Textures/Shadow/Post/Dev)로 점진 분리. 기능 변화 없이, 작은 diff로.
- **one-shot 커맨드버퍼 헬퍼** — `begin/endSingleTimeCommands`로 copy/transition/mipmap/upload boilerplate 통합(graphics queue 전용 시작).
- **리소스 수명/descriptor/pipeline 정리** — RAII·deletion queue 정책 문서화, descriptor set layout/binding 번호 문서화 + desync 위험 제거, 비활성 grass shadow 리소스 정리, pipeline cache.
- **shader interface 단일화** — UBO layout/alignment(std140/std430) 문서화, common GLSL include, shadow 상수 uniform화.
- **상수/설정 시스템** — compile-time(FFT N) vs runtime graphics(shadow size/AA/reflection) vs gameplay(wind/ship stats) 분류, `EngineSettings` 확장.
- **dev profiling/RenderDoc** — GPU timestamp 구간 확대(ocean/shadow/reflection/scene/water/post/UI), CPU timing, draw count, debug utils labels + object names, validation warning 0 목표.
- **테스트 최소 자동화** — pure logic 테스트(ship physics step / cargo capacity / market buy-sell / save load / wind efficiency)용 작은 test executable(새 의존성 없이).
- **save migration 정책** — version table(v1 ship → v2 cargo/money → v3 ports/market → v4 upgrades/contracts), corruption test(truncated/wrong magic/NaN/huge).
- **legacy farm code 최종 제거** — `ItemType`/`TileType`/`World` 실행 경로 0, farm asset 정리. legacy는 기록/별도 reference로만.
- **폴더 구조 재정리** — `src/engine`·`src/game`·`src/app`로(의존성 방향 `game → engine`). **기능 안정 후에만** 대규모 이동.
- **데이터 주도화 준비** — trade goods/ports/ship defs/upgrades 외부화 검토(과한 data engine 금지).
- **release/dev 빌드 + 품질 tier** — dev-only(ImGui/labels/profiler) 분리, settings persistence, High(RTX 3060 기본)/Medium/Low(디버그용) tier. **품질을 싸게 낮추는 게 아니라 비싼 기능을 명시적으로 tier화.**

---

## Phase 10 — 게임 완성도

**목표:** UX·튜토리얼·진행 목표·플레이 테스트 루프. (장기 — 핵심 루프가 굳은 뒤 상세화)

- 첫 항해/첫 교역 온보딩, 목표·진행도 제시, 항해 보조 UX 다듬기, 플레이 테스트 기반 밸런스 반복.

---

## 첫 작업 단위 (요약)

가장 좋은 첫 PR:

```text
기본 ShipState 추가 + WASD를 throttle/rudder로 해석 + 선박 관성 이동 1차  (Phase 1a~1c)
```

여기서 하지 않는 것: 항구·교역·저장 구조 교체·UI 전면 개편·TAA·async.
성공 기준: **배가 관성과 선회반경을 가진 선박처럼 움직인다.**
