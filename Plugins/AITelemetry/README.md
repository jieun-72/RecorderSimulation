# 장기 은신 후 요괴 랜덤 순찰 검증

## 목표

자연스러운 행동을 위해 적용한 요괴의 랜덤 순찰이, 플레이어가 한곳에 오래 숨은 뒤에도 정상적인 게임 흐름으로 이어지는지 확인한다. 은신 중 요괴가 멀어지더라도 플레이어가 맵 순찰을 재개하면 다시 감지·추적이 발생하는지 검증한다.

## 핵심

요괴의 순찰 목적지 선택을 다음처럼 변경했다.

| 상황 | 기존 방식 | 변경된 방식 |
|---|---|---|
| 평상시 순찰 | 요괴 현재 위치에서 반경 1,000cm 안의 도달 가능한 지점을 무작위 선택 | 기존 방식 사용 |
| 접촉이 오래 끊긴 순찰 | 같은 방식으로 근처 지점을 계속 선택 | 접촉 공백이 40초 이상이고 쿨다운이 끝나면, 플레이어가 있는 넓은 구역의 후보 지점 중 무작위 선택 |
| 추적 종료 직후 | 별도 순찰 보정 쿨다운 없음 | 추적이 끝난 시점부터 10초간 구역 보정 차단. 접촉 공백 40초 조건도 별도로 충족해야 함 |
| 구역 후보에 도달할 수 없음 | 별도 구역 후보 없음 | 기존 반경 1,000cm 랜덤 순찰로 복귀 |
| 이동 중 플레이어 위치 변경 | 이미 선택한 순찰 목표로 이동 | 다음 목표 선택 시점에만 플레이어 구역을 다시 확인 |

보정은 현재 플레이어가 속한 구역을 이용해 순찰 방문 장소를 조절한다. 은신 자체를 감지하는 기능은 아니며, 은신 시간과 접촉 공백 시간은 다를 수 있다. 실제 시각·청각 감지와 조사·추적 처리는 기존 흐름을 사용한다. 쿨다운 중에도 실제 감지에 의한 추적은 가능하다.

- AI를 활용해 만든 로거 플러그인으로 요괴·플레이어 위치, Patrol·Investigate·Chase 상태, 시각·청각 감지 이벤트를 CSV로 기록한다.
- 같은 집에서 10초·30초·50초 숨은 뒤 같은 경로와 목표로 플레이한다. 조건별 2회씩, 총 6판을 먼저 진행한다. 90초 은신은 제한시간 안에서 가능한 극단 조건으로 추가한다.
- 은신은 자동 판별하지 않는다. 판별 은신 조건과 집에서 나온 게임 경과 시각을 메모하고, 기록 시작 시각과의 차이를 확인한다.
- 핵심 지표는 은신 종료 후 첫 Chase까지 걸린 시간, 활동 시간 대비 Chase 시간 비율, 게임 종료까지 Chase가 없었던 판 수다. 미조우 판의 남은 관찰 시간도 함께 기록한다.
- 요괴–플레이어 거리와 이동 경로는 미조우 원인을 확인하는 보조 지표로 사용한다. Chase 전환 횟수만으로 정상 동작을 판단하지 않는다.
- 기존처럼 StartMenu → Main → Day_4로 플레이한다. Day_4 스트리밍 활성화 시 기록을 시작하고, 숨김·언로드·플레이 종료 시 저장한다.
- 저장 위치: `Saved/AITelemetry/<UTC시각_GUID>/samples.csv`, `events.csv`. 분석에는 `map=/Game/Day_4`, `run_kind=playtest`인 실제 플레이 기록만 사용한다.
- 순찰 보정: 접촉 공백이 T초를 넘고 추적 종료 쿨다운 C초가 끝나면, 다음 순찰 목표를 플레이어 구역의 후보 중 무작위로 선택한다. 이동 중 목표는 갱신하지 않으며 조사·추적 목표를 덮어쓰지 않는다. 유효 구역이나 도달 가능한 후보가 없으면 기존 반경 1,000cm 랜덤 순찰로 돌아간다.
- 접촉 시간은 시각 감지·추적이 유지되는 동안 갱신하고, 청각은 새 성공 이벤트 시각으로 갱신한다. 청각 감지가 계속 true인 것만으로 보정을 차단하지 않는다. 쿨다운은 보정만 차단하며 실제 감지로 인한 추적은 허용한다.
- `Project Settings → Plugins → Day 4 AI Telemetry` 또는 `Config/DefaultAITelemetry.ini`에서 보정을 설정한다. 현재 ON, T=40초, C=10초, `Condition=after`다. 기준선은 보정 OFF와 `Condition=baseline`으로 수집하고, C와 구역을 고정한 채 T=20/40/60초를 비교한다.
- `events.csv`에 보정 활성화·해제, 순찰 목표 선택, 이동 도착·실패·중단을 기록한다. 추가 열은 `patrol_bias_enabled`, `patrol_mode`, `patrol_zone`, `target_x/y/z_cm`, `contact_threshold_s`, `chase_cooldown_s`, `contact_gap_s`, `cooldown_remaining_s`다.

## 결과

- Main에서 스트리밍한 Day_4의 CSV 생성과 종료 저장을 실제 플레이로 확인했다.
- 실행 중인 복제 BT를 원본 객체와 비교해 상태가 Unknown으로 기록되던 문제를 수정했다. 실제 엔진 복제 노드의 상태 분류와 CSV 자동 검증을 통과했다.
- 수정 후 실제 플레이 3판에서 Patrol·Investigate·Chase 분류와 SessionEnd 저장을 확인했다.
- 기존 3판은 자유 플레이 기록이다. 은신 시간별 비교 테스트는 아직 수행하지 않았으며, 랜덤 순찰 유지 또는 보완 여부는 해당 테스트 후 결정한다.
- 구역 보정과 이동 결과 기록을 구현하고 BT·Day_4 맵에 연결했다. Main 스트리밍 월드의 자동 시뮬레이션에서 접촉 공백 40.8초 후 구역 보정 활성화와 보정 목표 도착 2회를 확인했다. 이 시험은 관전자 Pawn을 사용한 이동 기능 검증이며 실제 플레이의 조우 효과를 입증하지 않는다. 시험 CSV는 `Condition=validation`, `TesterId=automation`으로 구분하며 분석 표본에서 제외한다.
- 접촉 공백·추적 종료 쿨다운 경계값, BT 전환 중 Unknown 처리, 구역 경계, 실제 복제 BT 분류와 확장 CSV 검증을 포함한다. 저장된 에셋의 Task 연결과 6개 구역도 별도 프로세스로 재로드해 확인했다.

| 변경 위치 | 변경 내용 |
|---|---|
| `/Game/BP_Object/AIGhost/BT_Ghost` | 순찰 분기의 목적지 선택을 `UBTTask_SelectPatrolWithBias`, 이동을 `UBTTask_PatrolMoveWithLog`로 교체. 기존 이동 설정과 대기 Task 유지 |
| `/Game/Day_4` | `AI/PatrolBiasZones` 폴더에 범위와 순찰 후보 좌표를 가진 6개 구역 Actor 배치 |
| `Plugins/AITelemetry/Source/AITelemetry/Private/PatrolBias.cpp` | 접촉 공백·추적 종료 쿨다운, 구역 후보 선택, 경로 검증과 일반 순찰 복귀 구현 |
| `Plugins/AITelemetry/Source/AITelemetry/Private/AITelemetry.cpp` | CSV에 보정 설정·구역·선택 목적지·이동 결과 기록 추가 |
| `Config/DefaultAITelemetry.ini` | 보정 ON, T=40초, C=10초, `Condition=after`, `BuildLabel=patrol_bias_t40_c10` 설정 |

기준선 비교 시 `bEnablePatrolBias=False`, `Condition=baseline`으로 설정한다. 목적지 선택은 기존 로컬 랜덤 방식으로 돌아가며 추가 로그는 계속 남는다.

## 보완점

- 초기 구역은 집별 방 경계가 아닌 맵의 넓은 6개 구역이다. Day_4의 `AI/PatrolBiasZones`에서 범위와 `CandidateOffsets`를 편집한다. 후보는 실행 시 NavMesh에 투영하고 완전한 경로가 있는 경우만 사용한다. 구역 보정은 현재 플레이어 구역을 이용하는 조우 조절 기능이다.
- BT 순찰 분기의 선택·이동 Task를 교체하며 기존 이동 설정을 유지한다. 원본 BT·Day_4 맵 백업은 `Saved/PatrolBiasBackups/`에 보관한다. 다른 일차에서는 기존 로컬 랜덤 선택을 사용한다.

- 은신 시간이 길수록 남은 시간이 짧아지므로 Chase 총횟수보다 첫 Chase까지 시간과 활동 시간 대비 비율을 비교한다. 결과가 크게 갈리는 조건은 추가 반복한다.
- 시각·청각을 합친 EncounterStart는 청각 감지가 유지될 때 새 조우를 구분하지 못할 수 있다. Chase 상태와 새 시각 감지 이벤트를 함께 확인한다.
- 요괴가 특정 구역만 반복하면 최근 방문 구역의 선택 확률을 낮추고 오래 방문하지 않은 구역을 우선한다. 구역 사이 이동도 순찰 후보에 포함한다.
- 플레이어의 필수 이동 경로와 순찰 경로가 겹치지 않으면 출입구·교차로·목표 연결 통로의 순찰 가중치를 조정한다.
- 이동 정체는 NavMesh·이동 실패 처리, 근접해도 미감지하는 현상은 감지 설정, 짧게 끊기는 추적은 감지 상실·추적 종료 처리부터 확인한다. 원인 확인이 부족하면 순찰 목표 좌표와 이동 성공·실패 로그를 추가한다.
- 문제 원인에 맞는 보정 하나를 적용한 뒤 같은 은신 조건으로 전후 비교한다. 재조우 개선과 함께 정상 플레이에서 추적이 과도하게 늘지 않았는지도 확인한다.

### Last-seen investigation state

- The four-branch BT_Ghost records Chase, InvestigateLastSeen (last-seen location), Investigate (noise), and Patrol. All move, wait, and clear tasks inherit their branch state. Legacy three-branch trees remain supported.
- Inactive or unrecognized trees still report Unknown.
