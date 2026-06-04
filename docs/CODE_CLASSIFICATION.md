# 기존 코드 분류

이 문서는 Pastel-Farm에서 복제된 현재 코드베이스를 OceanVoyage 기준으로 분류한다.

목적은 바로 파일을 삭제하는 것이 아니라, 어떤 코드를 유지하고 어떤 코드를 나중에 제거하거나 교체할지 판단 기준을 세우는 것이다.

---

## 1. 유지할 코드

OceanVoyage에서도 거의 그대로 재사용할 가능성이 높은 코드다.

### `src/platform/`

분류: 유지

이유:

- 윈도우 생성
- 입력 처리
- 키보드 / 마우스 상태 관리
- 플랫폼 계층

OceanVoyage에서도 그대로 필요하다.

주의:

- 입력 시스템은 “키가 눌렸다”까지만 담당해야 한다.
- “배 전진”, “돛 조작”, “항구 상호작용” 같은 의미 해석은 game 계층에서 담당해야 한다.

---

### `src/renderer/`

분류: 유지

이유:

- VulkanContext
- 렌더링 리소스 생성 / 파괴
- 셰이더 로딩
- 파이프라인 구성
- 후처리
- 그림자
- SMAA / FXAA
- GPU 프로파일링
- ImGui 디버그 UI

OceanVoyage의 핵심 기반이다.

주의:

- 현재 렌더러가 `TileType`, `ObjectType`, 농장 전용 월드 데이터에 직접 의존하고 있다면 장기적으로 분리해야 한다.
- 렌더러는 최종적으로 게임 월드를 직접 읽지 않고 `FrameRenderData`만 받아야 한다.
- 바다 렌더링을 추가하기 전에 sRGB, mipmap, anisotropic filtering, HDR scene target, shadow 설정을 정리해야 한다.

---

### `third_party/`

분류: 유지

이유:

- `stb`
- `smaa`
- 기타 외부 라이브러리

현재 렌더러와 에셋 로딩에 필요하다.

주의:

- 사용하지 않는 라이브러리는 나중에 제거한다.
- 지금은 삭제하지 않는다.

---

### `shaders/post*`

분류: 유지

이유:

- 후처리 기반은 OceanVoyage에서도 필요하다.
- 바다, 하늘, 안개, 야간 조명, bloom 후보까지 고려하면 post pipeline은 중요하다.

주의:

- 장기적으로 HDR scene target과 톤매핑 구조를 고려해야 한다.

---

### `shaders/smaa*`

분류: 유지

이유:

- RTX 3060급 기준에서 SMAA는 가벼운 기본 AA 후보로 유지할 가치가 있다.
- MSAA보다 부담이 낮고, 현재 구조에 이미 포함되어 있다.

주의:

- 기본 AA 모드를 OFF로 둘지, FXAA/SMAA로 둘지 나중에 결정해야 한다.
- 단, 사실적 해수면에는 TAA가 더 적합하므로 최종 AA 방향은 `DESIGN.md`의 Visual North Star를 따른다.

---

## 2. 임시 유지할 코드

농장 게임 전용 성격이 있지만, OceanVoyage 시스템을 만들 때 참고 가치가 있는 코드다.

### `src/world/`

분류: 임시 유지

현재 역할:

- 청크 관리
- 지형 생성
- 타일 상태
- 오브젝트 상태
- 저장 / 로드
- 작물 성장 일부

OceanVoyage 대응 개념:

- OceanWorld
- Region
- OceanPatch
- Island
- Harbor
- PortArea

유지 이유:

- 청크 기반 월드 관리 구조는 바다 / 섬 / 항구 스트리밍에도 참고할 수 있다.
- 저장 / 로드 검증 구조도 참고 가치가 있다.
- 월드 수정 상태를 저장하는 방식은 항구 / 회사 / 화물 저장으로 확장 가능하다.

주의:

- 현재 `World`는 농장 타일 중심이다.
- 그대로 OceanVoyage에 쓰기보다는 새 `OceanWorld`를 만들 때 참고하는 쪽이 맞다.
- 작물, 밭, 물주기, TileType 중심 구조는 나중에 제거 또는 교체한다.

---

### `src/game/GameState.*`

분류: 임시 유지

현재 역할:

- 플레이어 상태
- 입력 처리
- 이동
- 상호작용
- 인벤토리
- UI 상태
- 월드와 렌더러 사이의 게임 진행 흐름

OceanVoyage 대응 개념:

- VoyageGameState
- SailingState
- PortState
- CompanyState
- CargoState

유지 이유:

- 게임 루프 흐름을 참고할 수 있다.
- 입력을 게임 동작으로 변환하는 구조를 참고할 수 있다.
- 렌더링에 넘길 데이터를 구성하는 흐름을 확인할 수 있다.

주의:

- 현재 GameState는 농장게임 규칙에 강하게 묶여 있다.
- 장기적으로는 새 GameState로 교체하는 것이 맞다.
- 플레이어 이동은 선박 이동으로 대체된다.
- 인벤토리는 화물창 / 창고 / 회사 재고로 대체된다.

---

### `src/game/Camera.*`

분류: 유지 또는 임시 유지

현재 역할:

- 카메라 위치
- 카메라 방향
- 뷰 / 프로젝션 관련 기능

OceanVoyage 대응 개념:

- 자유 카메라
- 선박 추적 카메라
- 항구 카메라
- 전략 / 지도 카메라

유지 이유:

- 카메라 수학 자체는 재사용 가능성이 높다.
- OceanVoyage 초기 테스트 씬에도 바로 필요하다.

주의:

- 현재 카메라가 아이소메트릭 / 농장 뷰에 맞춰져 있다면 확장 필요.
- 선박 추적 카메라, 궤도 카메라를 추가해야 한다.

---

### UI 관련 코드

분류: 임시 유지

현재 역할:

- 농장 게임 UI
- 인벤토리
- 툴 선택
- 디버그 표시

OceanVoyage 대응 개념:

- 화물창 UI
- 교역 UI
- 항구 UI
- 선박 상태 UI
- 회사 상태 UI

유지 이유:

- UI 렌더링 흐름을 참고할 수 있다.
- post 이후 UI 렌더링 구조는 유지 가치가 있다.

주의:

- 농장 전용 UI 내용은 제거 대상이다.
- UI 렌더링 시스템과 UI 내용물을 분리해야 한다.

---

## 3. 나중에 제거할 코드

OceanVoyage의 목표와 직접 관련이 낮은 농장 전용 시스템이다.

단, 최소 테스트 씬이 생기기 전까지는 대규모 삭제하지 않는다.

제거 후보:

- 작물 성장
- 밭 갈기
- 물주기
- 농기구
- 작업대 제작
- 울타리
- 농장 전용 아이템
- 농장 전용 오브젝트
- 농장 전용 타일 상호작용
- 농장 전용 UI 패널
- 농장 전용 save field

제거 조건:

1. OceanVoyage 최소 테스트 씬이 존재할 것
2. 빌드 가능한 대체 GameState가 있을 것
3. 렌더러가 농장 월드에 직접 의존하지 않을 것
4. 삭제 후에도 창이 뜨고 종료가 정상 동작할 것

---

## 4. 교체할 코드

기존 개념을 OceanVoyage 개념으로 바꿀 영역이다.

| 기존 개념 | OceanVoyage 개념 |
|---|---|
| Player | Ship / Captain / Company |
| Inventory | CargoHold / Warehouse / CompanyStorage |
| Crop | TradeGood / Resource |
| Workbench | Shipyard / Factory / PortFacility |
| Tile | OceanPatch / HarborCell / RegionCell |
| Farm Object | PortObject / ShipObject / IndustryObject |
| World | OceanWorld / RegionWorld |
| GameState | VoyageGameState |
| Save Data | VoyageSaveData |

---

## 5. 아직 삭제하지 말아야 할 것

다음은 이름만 농장스럽더라도 당장 삭제하지 않는다.

- `World`
- `Chunk`
- `GameState`
- `Inventory`
- `Object`
- `UI`
- `Save`
- `FrameRenderData`
- `VulkanContext`

이유:

- 지금은 이 코드들이 실행 가능한 프로그램을 구성하고 있다.
- OceanVoyage용 대체 시스템이 생기기 전까지는 삭제하면 빌드가 깨질 가능성이 높다.
- 일부는 새 시스템의 참고 자료로 유용하다.

---

## 6. 다음 실제 코드 작업 후보

문서 작업 이후 첫 코드 작업은 삭제가 아니라 최소 전환 준비 작업이어야 한다.

추천 순서:

1. 현재 실행 상태 빌드 확인
2. 창 제목만 Ocean Voyage로 변경
3. 최소 테스트 씬 플래그 추가 검토
4. GameState와 Renderer 사이의 데이터 경계 확인
5. 농장 전용 렌더 의존성 목록 작성
6. 그 다음 최소 Ocean test scene 추가

---

## 7. 현재 결론

현재 코드는 삭제 대상이 아니라 분류 대상이다.

OceanVoyage로 전환하려면 먼저 현재 코드의 역할을 정확히 구분해야 한다.

지금 단계의 핵심은 다음과 같다.

- 렌더러와 플랫폼 계층은 최대한 유지한다.
- 월드와 게임 상태는 임시 유지하면서 참고한다.
- 농장 전용 규칙은 최소 테스트 씬 이후 단계적으로 제거한다.
- 새 해상 게임 시스템은 기존 코드를 무작정 고치는 방식이 아니라, 대체 시스템을 만든 뒤 교체한다.