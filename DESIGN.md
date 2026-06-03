# DESIGN — Pastel Farm (Game Design Document)

> 이 문서는 **게임으로서 Pastel Farm이 무엇인지**(경험·비전·기둥·스코프)를 정의한다.
> 엔진 구조는 `ARCHITECTURE.md`, 변경 이력은 `DEVLOG.md`, 빌드·기능 스냅샷은 `README.md`.
>
> **갱신 시점:** 디자인 방향이 바뀔 때만(거의 안정적으로 유지). 프로젝트 방향을 흔들리지 않게 잡는 닻 역할.

---

## High Concept

Pastel Farm은 커스텀 Vulkan 엔진 위에 만드는 **고품질 스타일라이즈드 로우폴리 농사·라이프심** 게임이다.

> 기억에 남는 스타일라이즈드 세계에 살면서, 농사와 상호작용으로 땅을 가꾸고,
> "authored이면서 살아있는" 세계를 경험한다.

지향하지 **않는** 것:
- 무한 샌드박스 / 절차 생존 게임 / 마인크래프트식 탐험 게임

지향하는 것:
- 고정된 세계 정체성 · 분위기 · 농사 루프 · 세계 지속성 · 가벼운 서사 · 코지 진행

---

## Core Design Pillars

### 1. Remembered World (기억되는 세계)
세계 지리가 기억에 남아야 한다 — 숲·강·마을 랜드마크·자원 위치·길. 세계가 **authored**로 느껴질 것.

### 2. Stylized Atmosphere (스타일라이즈드 분위기)
사실성보다 비주얼 정체성. 로우폴리 / 플랫 셰이딩 / grounded lighting / 텍스처 디테일 / 따뜻한 분위기 / 읽히는 실루엣 / 강한 구도. 레퍼런스: 코지한 고품질 스타일라이즈드 아이소메트릭 월드.

### 3. Optimized Quality (최적화된 품질)
성능은 디자인 기둥이다. 그러나 목표는 초저사양 데모나 플래시게임식 단순화가 아니다. 최소 GTX 1050 Ti / 1080p / 60fps, 권장 GTX 1660 Super급을 기준으로, 최신 그래픽 기법을 선별해 **품질은 높고 성능은 안정적인** 스타일라이즈드 게임을 만든다.

### 4. Living World (살아있는 세계)
정적이지 않게 — day/night, 재생 자원, 식생 변화, wildlife, 계절 활동, 모션·앰비언스.
> 목표: **기억되는 세계 + 살아있는 세계**

---

## World Philosophy

**방향 (결정됨): Fixed World + Procedural Layer**

| 영구(Permanent) | 가변(Variable) |
|---|---|
| 지리·랜드마크·길·절벽·마을·authored 공간 | 자원·식생·wildlife·forage·계절 변화 |

서사 · 리플레이성 · 세계 기억성 · 살아있는 시스템의 균형.

> 참고: 현재 엔진 구현은 절차 생성 기반이며(스트리밍 청크), 고정맵은 맵 데이터/콘텐츠 준비 후 단계적으로 전환한다. (`ARCHITECTURE.md` 월드 모델 참조)

---

## World Structure (장기 모델)

```
World
├─ Terrain    지형·고도·절벽·고정 베이스 (voxel)
├─ TileState  가변 타일 상태: tilled / watered / fertility / moisture → 농경지
├─ StaticProp 환경물: tree / rock / bush / flower / fence (대부분 정적, 인스턴스)
├─ Crop       살아있는 농작물: stage / time / growth
└─ Entity     시뮬레이션: player / animal / NPC / tractor (이동·AI·상태·애니)
```

> 현재 구현 상태: Terrain·TileState·StaticProp(나무/돌/작업대/울타리/돌담)·save/load v2 존재. 작물은 임시로 voxel 타일(`WHEAT`)로 처리 중이며, 장기적으로 `Crop` 레이어로 분리 검토.

---

## Gameplay Loop (장기)

```
Explore → Gather → Farm/Build → Progress → Unlock → Return to world
```

지향: 차분함 · 시스템적 · 반복 가능 · 보람. 회피: 전투 중심 · 속도 중심 · 생존 압박.

---

## Farming Direction
땅 준비 → 경작(tilling) → 심기 → 물주기 → 성장 → 수확.
탐험·자원 수집·제작·진행과 자연스럽게 연결.

## Grid vs Organic World — 결정됨
- **게임 규칙은 grid를 따른다.** 농사 타일, 설치/철거, 충돌, 저장 좌표, 제작 오브젝트 배치는 예측 가능한 grid 기반이 맞다.
- **시각 경험은 100% grid처럼 보이면 안 된다.** Pastel Farm은 농사·라이프심이지 Minecraft식 블록 월드가 아니다. 자연 바닥, 숲 가장자리, 풀, 잔돌, 흙 패치, 자원 배치는 grid 위에 얹힌 유기적 레이어처럼 읽혀야 한다.
- 바닥 변주는 타일별 랜덤 색 변경으로 해결하지 않는다. 타일마다 색이 바뀌면 격자감이 더 강해진다.
- 자연스러운 breakup은 풀 clump, 잔돌, 흙/마른 풀 패치, 길 가장자리, 덤불·꽃·forage 같은 **비격자 dressing layer**로 만든다.
- 단, ground dressing의 최종 디테일은 큰 로우폴리 geometry 살포가 아니라 **작은 텍스처/알파 디테일과 낮은 대비의 placement layer**로 가야 한다. Step 7의 geometry patch는 배치 시스템 검증용 placeholder이며, 1차 스크린샷 기준 갈색 패치와 잔돌이 너무 크고 대비가 강해 지저분한 오브젝트처럼 읽혔다. cleanup 후에는 거의 안 보일 만큼 줄였지만, 최종 디테일을 텍스처/알파 기반으로 채우기 전 기준 화면으로는 이쪽이 더 적합하다.

## Resource Philosophy
자원은 재생한다. **영구 세계 + 지속 변경 + 재생 자원 레이어**. (돌 재생 / 계절 forage / wildlife 이동 / 식생 변화) → 세계가 고갈되지 않음.

## Building & Resource Interaction (Stardew-style) — 결정됨
- **지형은 불변** — 플레이어는 땅을 파거나 부수지 않는다(복셀 블록 파괴 없음). 지형/고도는 authored·고정.
- 자원은 **필드 오브젝트**(나무·돌 등). 도구로 채집(도끼→나무, 곡괭이→돌) → 오브젝트가 **아이템을 드롭** → 인벤토리로.
- **제작(crafting)**: 채집한 자원을 작업대에서 **설치 가능한 오브젝트**(울타리·구조물·장식)로 제작.
- **건축**: 제작 오브젝트를 월드에 설치, **플레이어가 설치한 오브젝트만 철거** 가능(지형·자연물은 자유 파괴 불가).
- 기존 Minecraft식 복셀 설치/파괴는 **은퇴** — 복셀 지형은 고정, 건축은 오브젝트 레이어에서 일어난다.

## Visual Direction
타깃: **고품질 스타일라이즈드 grounded 로우폴리**. 비주얼 퀄리티는 조명·그림자·분위기·구도·scene dressing·texture mapping·color grading에서 나온다. 포토리얼을 목표로 하지는 않지만, 품질 목표는 낮지 않다. A급 스타일라이즈드 상용 게임과 견줄 만한 화면을, 더 작고 안정적인 성능 예산 안에서 만드는 것이 목표다.

### Visual North Star — 결정됨
**레퍼런스: 스타일라이즈드 로우폴리 "디오라마" 룩 (예: 덕코프류) — 단, 그래픽만 참고하고 게임 시스템(전투 등)은 차용하지 않는다.**
- 핵심은 **art-directed lighting + 분위기 + 구도 + 세밀한 시각 레이어**다. 로우폴리는 저품질 제약이 아니라 스타일 선택이다.
- 추구: warm/cool 분리 조명, 따뜻한 톤, 읽히는 실루엣, 고품질 식생, 텍스처 기반 지면 디테일, 부드러운 후처리(과한 bloom·sharpen 회피), composition.
- **현 엔진은 이미 토대를 갖춤**(top/side vertex color + AO + shadow/PCF + day-night + fog + post-grading[exposure/contrast/saturation/split-tone/vignette] + FXAA + SMAA 1x). 자체 게임 UI는 후처리 이후에 그려 픽셀 폰트 선명도를 유지한다. 남은 격차는 **품질 패스와 에셋/텍스처 레이어**다. height fog, 텍스처 매핑, material-lite, 고품질 grass, wind, shadow 품질 옵션, SMAA diagonal/T2x/S2x 같은 고급 AA 품질 옵션을 성능 예산 안에서 단계적으로 도입한다. (기술 단계는 `ARCHITECTURE.md` 비주얼 로드맵)
- 지형이 내부적으로 voxel/grid 기반이어도 최종 화면은 grid를 과시하지 않는다. 농사/건축의 규칙성은 유지하되, 자연 환경은 dressing layer로 경계를 흐린다.
- 풀/식생은 Pastel Farm의 grid감을 줄이는 핵심 투자처다. 얇은 삼각형 몇 개를 세운 기하 풀은 멀리서 바늘처럼 보이기 쉬우므로 최종 방향이 아니다. 현재 alpha card 기반 clump와 density field 1차는 들어갔고, ground dressing 1차는 placement layer 검증과 placeholder 축소까지 완료했다. 다음 목표는 색/텍스처/card variation, 낮은 대비의 ground texture/detail, 약한 wind sway로 참고 이미지에 가까운 자연스러운 풀밭을 만드는 것.
- 전투 중심 연출(hit feedback, attack anticipation 등)은 **차용 안 함** — 코지 농사/라이프심엔 환경 연출(grass sway·leaf drift·발걸음/상호작용 피드백·ambient wildlife)만 가져온다.

---

## Long-Term Feature Direction
작물 · 날씨 · 계절 · 동물 · tractor/도구 · 마을/NPC · 가벼운 서사 · 환경 스토리텔링.
**비주얼·분위기 증폭기**(콘텐츠 재사용 가치 큼): photo mode · 날씨/계절 프리셋 · ambient wildlife(새·나비·반딧불) · 사운드 생태(바람·새·밤벌레).
> 모든 기능이 확정은 아니며 스코프는 의도적으로 통제한다.

## 멀티플레이 — v2 기둥 (post-prototype)
**협동(co-op) 멀티플레이를 장기 목표로 둔다.** 단, MVP/싱글 프로토타입이 우선이며 멀티는 핵심 메커닉이 굳은 뒤 **별도 v2 챕터**로 착수한다(콘텐츠가 불어나기 전이 최적 타이밍).
- 성격: 코지 협동(소수·비경쟁) — 경쟁 넷코드(롤백/렉보상) 불필요, 호스트/서버 권위로 충분.
- 지금은 구현하지 않되, 이를 가능케 하는 구조만 유지: 결정론 / 상태는 World·GameState 경유 / 이산 명령. (기술 상세는 `ARCHITECTURE.md` 멀티플레이 절)

## MVP Direction
초기 플레이 가능 버전 우선순위: **농사 · 고정/안정 맵 · save/load · 오브젝트 상호작용 · 분위기 · 기초 진행**.
이후에: 거대한 세계 · 대량 콘텐츠 · 고급 시뮬레이션.

---

## Final Design Statement

> 커스텀 엔진 위에, authored 지리와 살아있는 시스템이 공존하는,
> **스타일라이즈드하고 기억에 남는, 최적화된 고품질 농사 세계.**
