# 기존 코드 분류

이 문서는 Pastel-Farm에서 복제된 뒤 OceanVoyage로 전환 중인 현재 코드베이스를 OceanVoyage 기준으로 분류한다.

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

- ✅ `World&` 직접 의존, `Chunk`/`TileType`/`ObjectType` 기반 렌더 경로, inventory/hotbar/drops/workbench/targetTile 전달은 제거됐다.
- 현재 `FrameRenderData`는 카메라·선박·앱 상태·HUD 표시값을 전달한다. 렌더러가 교역 가격 계산이나 세이브 스키마를 직접 알지는 않지만, UI 그리기까지 `VulkanContext`에 있어 장기적으로는 UI/렌더 데이터 경계를 더 정리할 여지가 있다.
- ✅ sRGB, mipmap, anisotropic filtering, HDR scene target, shadow(CSM), FFT ocean, SSR/플래너 반사, PBR 선박, wake simulation, TAA 1차, SMAA 색공간 정리까지 구현됐다(`docs/ARCHITECTURE.md` §2.5, `DEVLOG.md`).

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

- 기본 AA는 현재 OFF/FXAA/SMAA 선택 구조이며, 기본값은 SMAA 계열로 유지할 가치가 있다.
- 단, 사실적 해수면 shimmer에는 TAA가 더 적합하므로 최종 AA 방향은 `DESIGN.md`와 `docs/ENGINE_TODO.md`의 Visual North Star를 따른다.

---

## 2. 임시 유지하거나 참고할 코드

농장 게임 전용 코드는 소스에서 대부분 제거됐다. 아래 항목은 현재 코드에 남아 있는 실행 경로와, 과거 구현에서 참고할 수 있는 개념을 분리해 본다.

### `src/world/`

분류: 제거 완료 / 기록 참고

현재 상태:

- `src/world/` 디렉터리는 제거됐다.
- `World`/`Chunk`/`TerrainGen`/`TileType` 실행 경로는 현재 소스에 없다.
- 청크 스트리밍·저장 검증 패턴은 `DEVLOG.md`와 git 이력에서만 참고한다.

OceanVoyage 대응 개념:

- OceanWorld
- Region
- OceanPatch
- Island
- Harbor
- PortArea

기록 참고 이유:

- 청크 기반 월드 관리 구조는 바다 / 섬 / 항구 스트리밍에도 참고할 수 있다.
- 저장 / 로드 검증 구조도 참고 가치가 있다.
- 월드 수정 상태를 저장하는 방식은 항구 / 회사 / 화물 저장으로 확장 가능하다.

주의:

- Phase 5에서 새 `OceanWorld`를 만들 때 과거 `World`를 되살리는 방식은 피한다.
- 항구·섬·풍향·항로는 새 OceanVoyage 데이터 모델로 도입한다.

---

### `src/game/GameState.*`

분류: 임시 유지

현재 역할:

- 선박 상태(`ShipState`)
- WASD → throttle/rudder 입력 해석
- 1차 항해 물리
- 항구/정박/시장 상태
- 화물창과 money
- 시간 진행
- 렌더러에 넘길 게임 표시값 구성의 기준 데이터

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

- 농장 게임플레이 행동, 제작, inventory/hotbar, drops, workbench 판정은 제거됐다.
- 현재 `GameState`가 선박 물리·항구·시장·화물·money를 모두 품고 있으므로, Phase 5~6에서 `OceanWorld`/`ShipDef`/경제 데이터가 커질 때 책임 분리를 검토한다.
- `PlayerInput`이라는 이름은 남아 있지만 의미는 선박/앱 입력이다. 이름 정리는 기능 변화 없는 별도 작업으로 다루는 편이 안전하다.

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

- 카메라는 이미 선박 스케일의 낮은 궤도 추적 파라미터로 조정돼 있고, 추적 대상도 `ShipState` 기반 `shipWorldPosition()`이다.
- 다음 카메라 작업은 자유 카메라/항구 뷰/전략 뷰 같은 새 모드가 필요해질 때 별도 설계한다.

---

### UI 관련 코드

분류: 임시 유지

현재 역할:

- 선박 HUD
- 항구/시장 UI
- 메뉴/설정/일시정지 UI
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

- UI 내용은 현재 `VulkanContext`의 벡터 폰트/quad 생성 코드에 직접 들어 있다.
- 장기적으로는 UI draw data 생성과 Vulkan 렌더링을 분리할 여지가 있지만, Phase 5의 우선순위는 항구·섬·풍향 같은 게임 세계 표현이다.

---

## 3. 제거 완료된 코드와 남은 잔재

OceanVoyage의 목표와 직접 관련이 낮은 농장 전용 시스템은 소스 실행 경로에서 제거됐다.

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

남은 잔재:

- 미사용 농장 에셋(`assets/textures/terrain`, `assets/textures/vegetation`) 확인 및 정리 후보
- 이름 잔재(`PlayerInput`, `PASTEL_DEV_BUILD`)
- 일부 주석의 `legacy`/`farm` 언급

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

## 5. 아직 임의로 삭제하지 말아야 할 것

다음은 현재 실행 경로에 필요하거나, 다음 단계의 기준점이므로 별도 작업 없이 삭제하지 않는다.

- `GameState`
- `VoyageSave`
- `FrameRenderData`
- `VulkanContext`
- 선박 모델/텍스처(`assets/models/ships/lsv018`)
- SMAA lookup texture 소스(`third_party/smaa`)

이유:

- 지금은 이 코드들이 실행 가능한 프로그램을 구성하고 있다.
- Phase 5 이후 새 `OceanWorld`/항구·섬 렌더 경로가 생기기 전에는 대체 없이 제거하면 기준 화면이 깨진다.

---

## 6. 다음 실제 코드 작업 후보

문서 정합성 정리 이후 첫 코드 작업은 삭제가 아니라 `docs/ROADMAP.md` Phase 5의 세계 표현 작업이어야 한다.

> 아래 추천 순서는 `docs/ROADMAP.md` Phase 5의 첫 작업 후보를 압축한 것이다. 티켓 단위 세부와 검증은 ROADMAP을 본다.

추천 순서:

1. 현재 실행 상태를 사용자 빌드로 확인
2. Phase 5 데이터 경계 확정: `OceanWorld`/`Port` 확장/`Island`/`Wind`/route target 중 첫 슬라이스 선택
3. 항구 시각 1차 또는 섬·풍향 1차를 작은 단위로 구현
4. 렌더러에는 게임 객체가 아니라 렌더 스냅샷/표시값만 넘기는 현재 방향 유지
5. 별도 정리 작업으로 미사용 농장 에셋과 이름 잔재 처리

---

## 7. 현재 결론

현재 코드는 더 이상 농장 코드 삭제가 핵심인 단계가 아니다.

OceanVoyage로 전환하려면 먼저 현재 코드의 역할을 정확히 구분해야 한다.

지금 단계의 핵심은 다음과 같다.

- 렌더러와 플랫폼 계층은 최대한 유지한다.
- `GameState`는 현재 프로토타입의 기준점으로 유지하되, Phase 5~6에서 커지는 책임은 새 데이터 구조로 분리한다.
- 새 해상 게임 시스템은 기존 코드를 무작정 고치는 방식이 아니라, `OceanWorld`/렌더 스냅샷 같은 대체 경계를 만든 뒤 연결 지점을 옮기는 방식으로 확장한다.
