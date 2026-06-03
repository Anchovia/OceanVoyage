# OceanVoyage 아키텍처 문서

이 문서는 OceanVoyage의 코드 구조와 엔진 / 게임 경계를 정리한다.

현재 저장소는 Pastel-Farm에서 복제된 초기 상태이므로, 아직 농장 게임 전용 코드와 재사용 가능한 엔진 코드가 섞여 있다.

이 문서의 목적은 무작정 파일을 삭제하기 전에 각 시스템의 역할을 분류하고, 이후 OceanVoyage 전환 과정에서 어떤 코드를 유지 / 임시 유지 / 제거 / 교체할지 판단하는 기준을 세우는 것이다.

---

## 1. 기본 원칙

OceanVoyage는 자체 Vulkan 엔진 위에서 동작하는 싱글플레이 해운·무역·산업 성장 RPG를 목표로 한다.

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

- `VulkanContext`는 엔진 코드로 유지하되, 현재는 일부 농장 게임 데이터에 의존할 수 있다.
- 렌더러가 `TileType`, `ObjectType`, `CropType` 같은 게임 전용 타입에 직접 의존한다면 장기적으로 제거해야 한다.
- 렌더러는 최종적으로 게임 상태가 아니라 렌더링용 스냅샷 데이터만 받아야 한다.

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

단, 제거는 최소 테스트 씬이 생긴 뒤에 한다.

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

## 13. OceanVoyage 초기 렌더링 목표

초기 테스트 씬은 다음을 목표로 한다.

- 하늘
- 안개
- 후처리
- 단순 수면
- 임시 선박
- 디버그 UI
- 자유 카메라 또는 궤도 카메라

이 단계에서는 아직 실제 게임 경제, 항구, 교역을 만들지 않는다.

먼저 “이 레포가 더 이상 농장게임이 아니라 바다 게임으로 전환 가능하다”는 기술적 기준점을 만든다.

---

## 14. 작업 순서

권장 작업 순서:

1. 문서 정리
2. 프로젝트 이름 / 타이틀 정리
3. 엔진 / 게임 경계 주석과 TODO 추가
4. 최소 테스트 씬 추가
5. 농장 시스템 제거 시작
6. OceanVoyage 전용 시스템 추가
7. 폴더 구조 재정리

---

## 15. 현재 결론

지금 단계의 핵심은 삭제가 아니다.

먼저 현재 코드가 어떤 역할을 하는지 분류하고, OceanVoyage에 필요한 엔진 기반을 보존하면서, 농장 게임 전용 코드를 안전하게 걷어내는 것이다.
