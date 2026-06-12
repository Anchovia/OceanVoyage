# OceanVoyage 아키텍처 문서

이 문서는 OceanVoyage의 코드 구조와 엔진 / 게임 경계를 정리한다.

현재 저장소는 Pastel-Farm에서 복제된 뒤 OceanVoyage로 전환 중이다. 렌더링 코어는 사실적 해양 방향으로 크게 진행됐고, 2026-06-12 기준 농장 게임 실행 경로(`World`/`Chunk`/`Player`/`TileType`/농장 UI/농장 save)는 소스에서 제거됐다. 남은 주요 과제는 OceanVoyage 전용 세계 표현(`OceanWorld`, 항구 시각, 섬, 풍향, 항로)과 이후 경제·선박 성장 구조를 쌓는 것이다.

이 문서의 목적은 현재 코드 구조와 엔진/game 경계를 정리하고, 이후 OceanVoyage 전용 시스템을 추가할 때 어떤 경계를 유지해야 하는지 판단하는 기준을 세우는 것이다.

> 코드 *구조*는 이 문서가, *개발 순서*(무엇을 어떤 순서로 만들지)는 `docs/ROADMAP.md`가 단일 출처다. 아래 신규 데이터 구조(`OceanWorld`/`Port`/`Island`/`CargoHold`/`ShipDef`/`VoyageSave` 등)의 세부 필드는 ROADMAP의 해당 Phase에 진입할 때 이 문서에서 확정한다.

---

## 1. 기본 원칙

OceanVoyage는 자체 Vulkan 엔진 위에서 동작하는 싱글플레이 해운·무역·산업 성장 RPG를 목표로 한다.

렌더링과 비주얼 구현의 1원칙은 **RTX 3060급 하드웨어에서 AAA/AA급 사실적 해양 게임을 만든다**는 것이다.

2원칙은 **실제 상용 게임과 엔진에서 쓰이는 안정적이고 확장 가능한 표준 기법을 우선한다**는 것이다.

따라서 렌더링 구현은 다음 기준을 따른다.

- 저사양 편법, 임시 화면 도색, 가짜 foam/wake, 저해상도 노이즈 블롭을 최종 기능처럼 넣지 않는다.
- 물 그래픽은 다중 스케일 FFT/Tessendorf, 물리 기반 셰이딩, SSR/플래너/환경 반사 조합, TAA/temporal 안정화, wake/foam mask 시뮬레이션처럼 실제 게임에서 쓰이는 경로로 확장 가능해야 한다.
- wake/foam은 단순 셰이더 수식으로 선을 그리는 방식이 아니라, 선박/파도/해안 입력이 별도 mask나 simulation pass에 기록되고 advection/decay/diffusion을 거쳐 해수면 셰이더가 샘플하는 방향을 기본으로 한다.
- "단순함"은 코드 구조의 과설계를 피하라는 의미이며, 렌더링 품질을 낮추거나 표준 기법을 생략하라는 의미가 아니다.
- 구현 전에 해당 기법이 최종 렌더러로 성장 가능한지, Vulkan 리소스/동기화/수명 구조가 안정적인지 확인한다.

따라서 코드 구조는 장기적으로 다음 두 계층으로 나누는 것을 목표로 한다.

```text
engine/
- 플랫폼
- 입력
- 렌더링
- 리소스
- 셰이더
- 디버그 UI
- 프로파일링
- 저장 기반 유틸리티

game/
- 선박
- 항해
- 항구
- 교역
- 화물
- 회사 성장
- 산업 경제
- 게임 진행 상태
```

현재는 아직 실제 폴더가 이 구조로 완전히 나뉘어 있지 않다.

따라서 초기 목표는 물리적인 폴더 이동보다 **의존성 방향을 정리하는 것**이다.

---

## 2. 의존성 방향

장기적으로 의존성은 다음 방향을 따라야 한다.

```text
game → engine
```

즉, 게임 코드는 엔진 기능을 사용할 수 있지만, 엔진 코드는 특정 게임 규칙을 알면 안 된다.

좋은 예:

```text
game/ShipController
→ engine/Input
→ engine/Renderer
```

나쁜 예:

```text
engine/Renderer
→ game/CropType
→ game/FarmingRule
```

렌더러는 작물, 밭, 교역품, 선박 같은 게임 규칙을 직접 알면 안 된다.

렌더러는 메시, 머티리얼, 인스턴스, 카메라, 조명 같은 렌더링 데이터만 받아야 한다.

---

## 2.5 현재 구현된 렌더 파이프라인 (2026-06-12 기준)

이 문서의 나머지(분류·전환 계획)는 대부분 전환 *방향*을 다루지만, 렌더링은 이미 사실적 해양 스택까지 구현되어 있다. 아래는 실제로 코드에 존재하는 프레임 그래프다(`VulkanContext_Frame.cpp::recordCommandBuffer`).

**프레임당 패스 순서**

```text
1. FFT 해양 컴퓨트 (월드 가시일 때만)        — 그래픽스 큐, 배리어로 렌더 앞에 직렬화
   스펙트럼 애니메이션 → butterfly IFFT(2·log2N 패스) → displacement/slope 조립
   → wake 시뮬 → GPU 5점 부력 샘플(20B host-visible 버퍼, 2프레임 지연 읽기)
2. 그림자 패스                              — CSM 3 캐스케이드 레이어별, 선박·항구 캐스터
3. 플래너 반사 패스                          — 물 평면 미러 카메라로 씬 1회 재렌더(항구는 REFL PLANAR/FULL에서만)
4. 불투명 씬 패스(offscreen HDR R16F)        — 하늘 → 항구(절차 메시) → 선박(pre-water refraction/depth seed)
5. scene color/depth 복사                    — 물의 굴절·깊이 샘플용
6. water 패스(LOAD)                          — 해수면(FFT) → 선박(최종 가시)
6.5 등대 볼류메트릭 빔 패스(LOAD, additive)  — scene depth 기반 풀스크린 레이마치, 밤 게이트
7. 후처리                                    — TAA 옵션(HDR resolve) 또는 SMAA(LDR 톤매핑 후 edge/blend/neighborhood) 또는 post FXAA/OFF → UI → (dev ImGui) → 스왑체인
```

**해양 컴퓨트 체인** (`VulkanContext_Ocean.cpp`, `shaders/ocean_*.comp`)

- 다중 캐스케이드 Tessendorf FFT: 초기 스펙트럼 h0(k) → per-frame 애니메이션 H(k,t) → radix-2 butterfly IFFT → displacement(xyz)+whitecap seed / slope(dH) 조립. 512² × 3 캐스케이드.
- wake: world-locked 토로이달 마스크에 선박 입력 주입 후 이류·확산·감쇠(ping-pong).
- 부력은 GPU compute가 선박 주변 5점 높이를 샘플해 20B host-visible 버퍼에 쓰고, CPU는 2프레임 지연으로 읽어 선박 높이/기울기에 사용.

**주요 리소스**

- offscreen HDR(R16G16B16A16_SFLOAT) 색 + depth, scene color/depth 복사본, 플래너 반사 타깃, TAA resolve 타깃, SMAA edge/blend/LDR 타깃 — 전부 frame-in-flight별.
- 그림자: depth 배열(레이어=캐스케이드), 2048².
- FFT: h0/spectrum/pong(RGBA32F) + displacement/slope(RGBA16F, 더블버퍼) + wake(RGBA16F) + 5점 부력 리드백 버퍼.
- 항구: 절차 port 메시(단일 vertex 버퍼, 인스턴스별 push constant model) + 공유 UBO의 로컬 라이트 8/스폿 4 배열(`shared_constants.h`, 등대 랜턴·부두 램프·스윕 빔).

**동기화·수명 모델**

- frame-in-flight = 2. per-frame in-flight fence + **per-swapchain-image** present 세마포어(이미지 수 변동 대응). 스왑체인 재생성 시 세마포어 재생성.
- 리소스는 `GpuBuffer`/`TextureResource` move-only RAII + `m_deletionQueue` 지연 해제.
- 해양 컴퓨트는 전용 compute 큐(async)가 아니라 그래픽스 큐에서 배리어로 직렬화한다 — 안정적이지만 오버랩 여지는 남아 있다.

> 알려진 렌더링 한계와 다음 과제(TAA 2차 보류, ocean mesh/buffer 정리, async compute 검토, 반사 세부 비용 정책 등)는 `docs/ENGINE_TODO.md`에 정리한다.

---

## 3. 현재 코드 분류 기준

현재 코드베이스는 다음 기준으로 분류한다.

---

## 4. 엔진 코드로 유지할 영역

OceanVoyage에서도 재사용할 가능성이 높은 코드다.

예상 영역:

```text
src/platform/
src/renderer/
shaders/post*
shaders/smaa*
일부 공통 shader
일부 math / utility 코드
```

대표 시스템:

- Window 생성과 이벤트 처리
- InputManager
- VulkanContext
- Vulkan 리소스 생성 / 파괴
- GpuBuffer
- TextureResource
- 셰이더 모듈 로딩
- 파이프라인 생성
- 후처리
- FXAA / SMAA
- GPU timestamp profiler
- ImGui / DevUI
- 기본 카메라 수학
- FrameRenderData 패턴

주의할 점:

- `VulkanContext`는 엔진 코드로 유지하되, 현재 파일이 매우 크고 UI 생성까지 포함한다.
- `World&` 직접 의존과 농장 `FrameRenderData` 필드는 제거됐다.
- 장기 목표는 렌더러가 게임 월드를 직접 읽지 않고, 카메라·조명·메시·머티리얼·UI draw data 같은 순수 렌더링 스냅샷만 받는 구조를 계속 유지하는 것이다.
- 따라서 다음 구조 작업의 1순위는 폴더 이동이 아니라 Phase 5의 새 `OceanWorld`/항구·섬 데이터를 렌더 스냅샷으로 변환하는 경계를 세우는 것이다.

---

## 5. 임시 유지할 영역

지금 바로 삭제하지 않고 참고용으로 유지할 코드다.

농장 게임 전용 성격이 있지만 OceanVoyage 시스템을 만들 때 구조를 참고할 수 있다.

현재 참고/유지 영역:

```text
src/game/
renderer 내부 UI 생성 코드
```

대표 시스템:

- `GameState`의 선박/항구/화물/시장 흐름
- `VoyageSave`의 atomic write + 검증 + commit 패턴
- renderer 내부 UI 생성 방식
- 기존 전환 기록(`DEVLOG.md`)에 남은 World/Chunk/세이브 검증 방식

OceanVoyage에서 대응되는 새 시스템:

```text
World / Chunk
→ OceanWorld / Region / Harbor / Island / OceanPatch

Inventory
→ CargoHold / Warehouse / CompanyStorage

Object placement
→ PortBuilding / HarborObject / ShipModule placement

Tile interaction
→ Port interaction / Trade interaction / Navigation interaction

GameState
→ VoyageGameState / CompanyState / SailingState
```

---

## 6. 제거할 영역

OceanVoyage의 목표와 직접 관련이 낮은 Pastel-Farm 전용 코드다.

단, 제거는 대체 선박 상태·렌더 데이터 경계·저장 구조가 생긴 뒤에 한다.

제거 후보:

- 작물 성장
- 밭 갈기
- 물주기
- 농기구
- 제작대
- 울타리
- 농장 전용 아이템
- 농장 전용 오브젝트
- 농장 전용 UI
- 농장 전용 타일 규칙

제거 원칙:

```text
1. 한 번에 많이 지우지 않는다.
2. 제거 전 대체 코드 또는 테스트 씬을 만든다.
3. 제거 후 즉시 빌드한다.
4. 작은 커밋으로 나눈다.
```

---

## 7. 교체할 영역

삭제가 아니라 OceanVoyage 버전으로 바꿔야 하는 영역이다.

```text
Pastel-Farm 개념        OceanVoyage 개념

Player                 Ship / Captain / Company
Inventory              CargoHold / Warehouse
Crop                   TradeGood / Resource
Workbench              Shipyard / Factory / PortFacility
Tile                   OceanPatch / HarborTile / RegionCell
Farm Object            PortObject / ShipObject / IndustryObject
World Save             Voyage Save
GameState              VoyageGameState
```

### 7.1 신규 데이터 구조 (도입 시 확정)

아래는 ROADMAP Phase 1·3·5·6에서 도입할 새 시스템의 1차 스케치다. 필드는 해당 Phase 진입 시 확정하며, 여기서는 경계와 책임만 잡는다.

```text
ShipState   (Phase 1) — position, velocity, heading, yawRate, throttle, rudder   // 현재 상태(가변)
ShipDef     (Phase 6) — cargoCapacity, maxSpeed, accel, turnRate, drag, draft    // 불변 스펙, upgrade는 modifier
CargoHold   (Phase 3) — capacity, stacks[]                                        // 인벤토리 대체, weight 제약
Port        (Phase 3) — id, name, position; (Phase 5) type, market
MarketEntry (Phase 3) — good, buyPrice, sellPrice, stock
Island      (Phase 5) — center, radiusX, radiusY, rotation                        // 충돌은 ellipse distance
Wind        (Phase 5) — direction, speed, gust                                    // gameplay 우선, FFT 연동 후순위
VoyageSave  (Phase 3) — magic "OVYG" + version. v1: gameTime + ShipState
                        v2: +money/cargo, v3: +ports/market, v4: +upgrades/contracts
```

- 이 구조들은 농장 `World/Chunk/TileType`/`Player`/`Inventory`를 대체하며, 렌더러는 이들을 직접 읽지 않는다(§9 렌더 스냅샷 경계).
- `OceanWorld`는 ports/islands/wind/region seed/discovered를 소유하고 렌더러를 모른다. renderer용 mesh 인스턴스는 `OceanWorld → RenderSceneSnapshot` 변환에서 생성한다.

---

## 8. 장기 목표 폴더 구조

최종적으로는 다음 구조를 목표로 한다.

```text
src/
  engine/
    platform/
    input/
    renderer/
    assets/
    ui/
    debug/
    profiler/
    serialization/

  game/
    core/
    ocean/
    ship/
    port/
    trade/
    economy/
    company/
    save/

  app/
    main.cpp
```

하지만 지금 당장 이 구조로 전부 옮기지는 않는다.

초기에는 현재 구조를 유지하면서 의존성을 줄이고, 기능 교체가 충분히 진행된 뒤 폴더 이동을 한다.

---

## 9. 렌더링 데이터 경계

렌더러는 게임 월드 객체를 직접 읽지 않는 방향으로 간다.

목표 구조:

```text
GameState / World
→ FrameRenderData 생성
→ Renderer가 FrameRenderData만 사용
```

`FrameRenderData`에는 다음과 같은 렌더링용 데이터만 들어가야 한다.

- 카메라
- 조명
- 시간대
- 안개
- 메시 인스턴스
- 머티리얼 ID
- 디버그 draw 정보
- UI draw 정보

들어가면 안 되는 것:

- 작물 성장 규칙
- 교역 가격 계산
- 선박 내구도 계산
- 저장 포맷 세부 정보
- 게임 진행 조건

---

## 10. 저장 시스템 경계

저장 시스템은 장기적으로 다음처럼 분리한다.

```text
engine/serialization
- 바이너리 읽기 / 쓰기 헬퍼
- 버전 검사 유틸리티
- 안전한 파일 저장
- atomic save helper

game/save
- 어떤 데이터를 저장할지 결정
- 게임 버전별 save schema
- OceanVoyage 전용 저장 구조
```

현재 Pastel-Farm의 save v3 구조는 참고할 가치가 있다.

특히 atomic save, version check, 범위 검증, 로드 후 commit 구조는 유지할 만하다.

---

## 11. 입력 시스템 경계

입력 시스템은 키보드 / 마우스 / 창 이벤트 같은 저수준 입력만 담당한다.

```text
engine/input
- 키 상태
- 마우스 위치
- 마우스 버튼
- 스크롤
- 창 focus 상태

game/input
- 배 가속
- 돛 조작
- 카메라 회전
- 항구 상호작용
- UI 단축키
```

즉, 엔진 입력 시스템은 “W 키가 눌림”까지만 알고, 게임 코드는 그것을 “배 전진”으로 해석한다.

---

## 12. 카메라 시스템

현재 카메라 코드는 재사용 가능성이 높다.

OceanVoyage에서는 최소 세 가지 카메라 모드가 필요할 수 있다.

- 자유 카메라
- 선박 추적 카메라
- 항구 / 전략 뷰 카메라

초기에는 자유 카메라와 선박 추적 카메라만 목표로 한다.

---

## 13. OceanVoyage 초기 렌더링 목표 — 렌더링 기준점 달성

초기 테스트 씬 목표(하늘·안개·후처리·수면·선박·디버그 UI·배 추적 궤도 카메라)는 렌더링 기준점 관점에서 **달성됐고, 사실적 해양 스택까지 구현됐다.** 현재 씬은 `ShipState` 기반 항해 상태와 `FrameRenderData` 렌더 스냅샷으로 동작하며, `World`/`Chunk` 기반 해상 테스트 월드는 제거됐다.

- 하늘/안개: 시간대·달 연동 procedural sky + 장거리 대기 fog — **구현됨**
- 후처리: HDR(R16F) + ACES 톤매핑 + 그레이딩 + FXAA/SMAA + TAA 1차 옵션 — **구현됨**
- 수면: **다중 캐스케이드 Tessendorf FFT + SSR/플래너 반사 + 프레넬 + Jacobian whitecap** (§2.5 참조)
- 선박: **PBR(Cook-Torrance GGX) 머티리얼 + CSM 그림자 + FFT 부력**
- wake/foam: **시뮬레이션 마스크 기반**(이류·확산·감쇠) — 원칙대로 셰이더 도색으로 가지 않음
- 카메라: UWO식 배 추적 궤도 — **구현됨** (`DESIGN.md` 참조)

즉 “이 레포가 더 이상 농장게임이 아니라 바다 게임으로 전환 가능하다”는 **기술적 기준점은 확보됐다.** `Player`/농장 이동 교체와 렌더러-게임 데이터 경계 정리는 완료됐고, 다음 본작업은 이 기준 화면 위에 기억 가능한 해상 공간(항구 시각·섬·풍향·항로)을 올리는 것이다.

이 단계에는 아직 경제·항구·교역이 1차 프로토타입 수준이다. BRISTOL/LIVERPOOL 두 항구와 시장 매매·화물·money·저장 복원까지는 동작하지만, 세계 표현과 동적 경제는 Phase 5~6 작업이다.

> 비주얼 방향은 `DESIGN.md`의 Visual North Star를 따른다. wake/foam/whitecap은 원칙대로 placeholder 셰이더 도색이 아니라 별도 시뮬레이션/마스크 기반으로 구현됐고, 앞으로의 고도화(스프레이 파티클, 해안 거품 등)도 같은 방향으로 확장한다.

---

## 14. 현재 권장 작업 순서

권장 작업 순서(요약):

1. Phase 5: `OceanWorld` 경계와 항구·섬·풍향·항로 데이터 도입
2. 항구 시각 1차(부두/창고/등대 등) 또는 섬·해안선 1차 구현
3. 풍향을 HUD와 항해 물리에 약하게 연결
4. Phase 4 잔여 렌더 과제(ocean mesh/buffer 정리, 기준 성능 측정, TAA 2차 보류 항목 재평가)를 작은 작업으로 진행
5. Phase 6: `ShipDef`/경제 데이터/업그레이드/계약으로 교역 루프 확장
6. 기능 교체가 충분히 진행된 뒤 폴더 구조 재정리

> 이 순서는 `docs/ROADMAP.md`의 Phase 5~9를 압축한 것이다. 단계별 세부 작업·검증·닿는 파일은 ROADMAP을 단일 출처로 본다.

### 14.1 엔진 구조 안정화 (ROADMAP Phase 9)

기능 교체가 충분히 진행된 뒤, 폴더 이동(§8)과 함께 다음 구조 정리를 진행한다. 세부는 `docs/ENGINE_TODO.md`.

- `VulkanContext_Init.cpp`(4000+줄)를 기능별 cpp(Swapchain/Pipelines/Textures/Shadow/Post/Dev)로 점진 분리 — 기능 변화 없이 작은 diff로.
- one-shot 커맨드버퍼 boilerplate를 `begin/endSingleTimeCommands`로 통합.
- descriptor set layout / binding 번호 문서화 + C++↔GLSL desync 위험 제거.
- shader interface 단일화: UBO alignment(std140/std430) 문서화, common GLSL include 확장, 런타임 품질 옵션과 셰이더 상수 경계 정리.
- save migration 정책(version table v1~v4 + corruption test).
- release/dev 빌드 분리 + 품질 tier(High=RTX 3060 기본 / Medium / Low=디버그용). 품질을 싸게 낮추는 게 아니라 비싼 기능을 명시적으로 tier화.

---

## 15. 현재 결론

지금 단계의 핵심은 농장 코드 삭제가 아니라 OceanVoyage 전용 세계를 쌓는 것이다.

현재 기준점은 사실적 해양 렌더링 + 선박 항해 + 2항구 교역 루프다. 다음 작업은 이 기준점을 보존하면서 `OceanWorld`/항구 시각/섬/풍향/항로를 추가하고, 렌더러가 계속 순수 렌더 스냅샷만 받도록 경계를 유지하는 것이다.
