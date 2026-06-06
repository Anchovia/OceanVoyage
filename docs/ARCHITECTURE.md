# OceanVoyage 아키텍처 문서

이 문서는 OceanVoyage의 코드 구조와 엔진 / 게임 경계를 정리한다.

현재 저장소는 Pastel-Farm에서 복제된 뒤 OceanVoyage로 전환 중이다. 렌더링 코어는 사실적 해양 방향으로 크게 진행됐지만, 게임플레이·월드·UI·저장 계층에는 아직 농장 게임 전용 코드와 재사용 가능한 엔진 코드가 섞여 있다.

이 문서의 목적은 무작정 파일을 삭제하기 전에 각 시스템의 역할을 분류하고, 이후 OceanVoyage 전환 과정에서 어떤 코드를 유지 / 임시 유지 / 제거 / 교체할지 판단하는 기준을 세우는 것이다.

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

## 2.5 현재 구현된 렌더 파이프라인 (2026-06-06 기준)

이 문서의 나머지(분류·전환 계획)는 대부분 전환 *방향*을 다루지만, 렌더링은 이미 사실적 해양 스택까지 구현되어 있다. 아래는 실제로 코드에 존재하는 프레임 그래프다(`VulkanContext_Frame.cpp::recordCommandBuffer`).

**프레임당 패스 순서**

```text
1. FFT 해양 컴퓨트 (월드 가시일 때만)        — 그래픽스 큐, 배리어로 렌더 앞에 직렬화
   스펙트럼 애니메이션 → butterfly IFFT(2·log2N 패스) → displacement/slope 조립
   → wake 시뮬 → displacement host 리드백 복사(부력용, 2프레임 지연 읽기)
2. 그림자 패스                              — CSM 3 캐스케이드 레이어별, 청크/오브젝트/선박 캐스터
3. 플래너 반사 패스                          — 물 평면 미러 카메라로 씬 1회 재렌더
4. 불투명 씬 패스(offscreen HDR R16F)        — 하늘 → 청크 → 지면 드레싱 → 풀 → 오브젝트 → 선박(pre-water seed)
5. scene color/depth 복사                    — 물의 굴절·깊이 샘플용
6. water 패스(LOAD)                          — 해수면(FFT) → 선택자/드롭 → 선박(최종 가시)
7. 후처리                                    — SMAA edge/blend(톤매핑 luma 기반 edge) → neighborhood/톤매핑+그레이딩 또는 post FXAA/OFF → UI → (dev ImGui) → 스왑체인
```

**해양 컴퓨트 체인** (`VulkanContext_Ocean.cpp`, `shaders/ocean_*.comp`)

- 다중 캐스케이드 Tessendorf FFT: 초기 스펙트럼 h0(k) → per-frame 애니메이션 H(k,t) → radix-2 butterfly IFFT → displacement(xyz)+whitecap seed / slope(dH) 조립. 512² × 3 캐스케이드.
- wake: world-locked 토로이달 마스크에 선박 입력 주입 후 이류·확산·감쇠(ping-pong).
- displacement는 host로 리드백해 CPU에서 선박 부력(수평 변위 역산 + 파면 높이/기울기)에 사용.

**주요 리소스**

- offscreen HDR(R16G16B16A16_SFLOAT) 색 + depth, scene color/depth 복사본, 플래너 반사 타깃, SMAA edge/blend 타깃 — 전부 frame-in-flight별.
- 그림자: depth 배열(레이어=캐스케이드), 2048².
- FFT: h0/spectrum/pong(RGBA32F) + displacement/slope(RGBA16F, 더블버퍼) + wake(RGBA16F) + host 리드백 버퍼.

**동기화·수명 모델**

- frame-in-flight = 2. per-frame in-flight fence + **per-swapchain-image** present 세마포어(이미지 수 변동 대응). 스왑체인 재생성 시 세마포어 재생성.
- 리소스는 `GpuBuffer`/`TextureResource` move-only RAII + `m_deletionQueue` 지연 해제.
- 해양 컴퓨트는 전용 compute 큐(async)가 아니라 그래픽스 큐에서 배리어로 직렬화한다 — 안정적이지만 오버랩 여지는 남아 있다.

> 알려진 렌더링 한계와 다음 과제(TAA 미도입, SMAA neighborhood가 아직 HDR scene color를 섞은 뒤 톤매핑하는 구조, 셰이더 상수 중복, 부력 전체 리드백, async compute 미사용 등)는 `docs/ENGINE_TODO.md`에 정리한다.

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

- `VulkanContext`는 엔진 코드로 유지하되, 현재는 실제로 `World&`를 생성자에서 받고 `Chunk`/`TileType`/`ObjectType` 기반 렌더 버퍼를 직접 관리한다.
- `FrameRenderData`도 아직 `inventory`, `hotbar`, `drops`, `nearWorkbench`, `targetTile` 같은 농장/플레이어 상태를 포함한다.
- 장기 목표는 렌더러가 게임 월드를 직접 읽지 않고, 카메라·조명·메시·머티리얼·UI draw data 같은 순수 렌더링 스냅샷만 받는 구조다.
- 따라서 다음 구조 작업의 1순위는 폴더 이동이 아니라 `FrameRenderData` 정리와 `World` 직접 참조 제거다.

---

## 5. 임시 유지할 영역

지금 바로 삭제하지 않고 참고용으로 유지할 코드다.

농장 게임 전용 성격이 있지만 OceanVoyage 시스템을 만들 때 구조를 참고할 수 있다.

예상 영역:

```text
src/world/
src/game/
src/ui/
```

대표 시스템:

- World / Chunk 관리
- 저장 / 로드 포맷 처리
- 인벤토리
- 오브젝트 배치
- 타일 상호작용
- UI 생성 방식
- GameState 흐름

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

초기 테스트 씬 목표(하늘·안개·후처리·수면·선박·디버그 UI·배 추적 궤도 카메라)는 렌더링 기준점 관점에서 **달성됐고, 사실적 해양 스택까지 구현됐다.** 단, 이 씬은 아직 독립적인 OceanVoyage 게임 상태가 아니라 `World`/`Chunk` 기반 해상 테스트 월드 위에서 동작한다.

- 하늘/안개: 시간대·달 연동 procedural sky + 장거리 대기 fog — **구현됨**
- 후처리: HDR(R16F) + ACES 톤매핑 + 그레이딩 + FXAA/SMAA — **구현됨**
- 수면: **다중 캐스케이드 Tessendorf FFT + SSR/플래너 반사 + 프레넬 + Jacobian whitecap** (§2.5 참조)
- 선박: **PBR(Cook-Torrance GGX) 머티리얼 + CSM 그림자 + FFT 부력**
- wake/foam: **시뮬레이션 마스크 기반**(이류·확산·감쇠) — 원칙대로 셰이더 도색으로 가지 않음
- 카메라: UWO식 배 추적 궤도 — **구현됨** (`DESIGN.md` 참조)

즉 “이 레포가 더 이상 농장게임이 아니라 바다 게임으로 전환 가능하다”는 **기술적 기준점은 확보됐다.** 다음 본작업은 이 화면 위에 바로 경제/항구를 얹기 전에, `Player`/농장 이동을 `ShipState`/항해 물리로 교체하고 렌더러-게임 데이터 경계를 정리하는 것이다.

이 단계에서도 아직 실제 게임 경제·항구·교역은 만들지 않았다.

> 비주얼 방향은 `DESIGN.md`의 Visual North Star를 따른다. wake/foam/whitecap은 원칙대로 placeholder 셰이더 도색이 아니라 별도 시뮬레이션/마스크 기반으로 구현됐고, 앞으로의 고도화(스프레이 파티클, 해안 거품 등)도 같은 방향으로 확장한다.

---

## 14. 현재 권장 작업 순서

권장 작업 순서:

1. `ShipState`/`ShipController` 또는 동등한 항해 상태 모델 도입
2. 농장 타일-워크 이동을 관성·타각·풍향/추진 입력 기반 선박 이동으로 교체
3. `FrameRenderData`를 순수 렌더 스냅샷으로 줄이고 `VulkanContext`의 `World&` 직접 의존 제거
4. 농장 UI/인벤토리/제작/세이브 필드를 화물·선박·회사 상태로 교체하거나 제거
5. 렌더링 후속 과제(TAA, 리드백 축소, 반사 비용 정책, 셰이더 상수 단일화)를 작은 작업으로 진행
6. 항구·시장·교역 루프 추가
7. 기능 교체가 충분히 진행된 뒤 폴더 구조 재정리

---

## 15. 현재 결론

지금 단계의 핵심은 삭제가 아니다.

먼저 현재 코드가 어떤 역할을 하는지 분류하고, OceanVoyage에 필요한 엔진 기반을 보존하면서, 농장 게임 전용 코드를 안전하게 걷어내는 것이다.
