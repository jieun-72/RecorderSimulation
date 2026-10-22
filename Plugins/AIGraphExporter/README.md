# 읽기 전용 AI 그래프 추출 (UE 5.4.4)

이 프로젝트의 `AIC_Ghost`, `BT_Ghost`를 JSON으로 추출하는 Editor 전용 플러그인입니다. 게임 런타임에는 포함되지 않습니다. 프로젝트의 기존 `.uproject` 설정을 바꾸지 않고 `EnabledByDefault`로 활성화됩니다. 새 플러그인을 발견하려면 에디터 재시작이 필요할 수 있습니다.

## 수동 실행

UE 5.4 Editor 타깃을 먼저 빌드합니다. 경로는 설치 환경에 맞게 조정하세요.

```powershell
& 'C:/Program Files/Epic Games/UE_5.4/Engine/Build/BatchFiles/Build.bat' RecorderSimulationEditor Win64 Development 'C:/UnrealProject/GitHub/clone/RecorderSimulation/RecorderSimulation.uproject' -WaitMutex -NoHotReloadFromIDE
```

플러그인이 로드된 에디터의 콘솔에서:

```text
AIExports.ExportGhostGraphs
```

또는 에디터 UI 없이 저장된 에셋을 읽는 Commandlet:

```powershell
& 'C:/Program Files/Epic Games/UE_5.4/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/UnrealProject/GitHub/clone/RecorderSimulation/RecorderSimulation.uproject' -run=AIGraphExport -unattended -nullrhi -nosplash -NoSound
```

성공하면 종료 코드 0과 `AI_GRAPH_EXPORT_RESULT` JSON 상태를 반환합니다. 실패하면 종료 코드 1을 반환합니다. 기존 에디터에서의 메모리상 수정과 Commandlet이 읽는 디스크상의 내용은 다를 수 있습니다.

## MCP / Python 호출

현재 Unreal MCP의 `ue_exec_python`에서 실행할 코드:

```python
import unreal
result = unreal.AIGraphExportLibrary.export_ghost_graphs()
```

개별 에셋도 호출할 수 있습니다.

```python
result = unreal.AIGraphExportLibrary.export_blueprint_graph('/Game/BP_Object/AIGhost/AIC_Ghost')
result = unreal.AIGraphExportLibrary.export_behavior_tree('/Game/BP_Object/AIGhost/BT_Ghost')
```

`unreal.AIGraphExportLibrary`가 없다면 플러그인이 현재 프로세스에 로드되지 않은 것입니다. 에디터 재시작 후 다시 확인하세요. 성공 반환에는 실제 JSON 경로가 들어 있습니다. Remote Control의 Python 호출 허용 설정이 필요합니다.

## JSON 출력

출력은 항상 프로젝트 `Saved/AIExports`에 저장됩니다. 파일 이름은 에셋 이름, 에셋 경로의 해시, 종류를 사용하므로 같은 이름의 다른 에셋을 구분합니다. 같은 에셋을 재추출하면 해당 JSON만 덮어씁니다.

- Blueprint: 로컬 이벤트/함수/매크로/중첩 그래프, 노드 클래스·GUID·제목·위치, 핀 ID·방향·타입·기본값·분할 핀·연결. 함수 호출과 이벤트의 참조 이름·소유 클래스·Blueprint 경로·해결된 함수 경로. 기타 노드 설정은 `serialized_properties`에 포함됩니다.
- Behavior Tree: 저장된 루트, `children` 배열 순서(`child_index`), Task, Decorator, Composite/Task Service, 루트 Decorator와 논리 연산, 노드 설정, Blackboard와 부모의 키 정의. `FBlackboardKeySelector`의 `SelectedKeyName`은 `settings` 안에 구조적으로 저장됩니다. 관련 Blueprint 경로와 BT 에디터 그래프도 포함됩니다.
- 메타데이터: 엔진 버전, 시각, 패키지 dirty flag, `unavailable_information`, `limitations`.

실행 핀은 `kind: execution`, 데이터 핀은 `kind: data`입니다. 각 `links` 항목의 `node_path`와 `pin_id`로 대상 핀을 찾습니다. 입력과 출력 양쪽의 원래 연결을 보존합니다. 연결된 핀의 기본값은 실행 시 사용되지 않을 수 있습니다.

스칼라·enum·이름·map·set·delegate 설정은 정확한 Unreal 텍스트와 `cpp_type`, `representation: unreal_property_text`로 저장합니다. 숫자나 enum 의미를 임의로 해석하지 않습니다. UObject 참조는 경로로만 저장하여 부모/소유자 순환을 따라가지 않습니다.

## 읽기 전용 경계와 한계

`LoadObject`, 기존 그래프/프로퍼티 읽기, JSON 파일 쓰기만 수행합니다. `Modify`, `MarkPackageDirty`, `SavePackage`, Blueprint compile/reconstruct, BT rebuild/initialize/resolve는 호출하지 않습니다. Unreal 자체의 로딩/PostLoad 처리는 발생할 수 있습니다. 검사 중 대상 패키지의 dirty flag가 바뀌면 성공으로 보고하지 않습니다. dirty flag를 되돌리거나 원본 에셋을 저장하지 않습니다.

- 실시간 AI 실행 상태, 감지 이력, Blackboard 런타임 값은 추출하지 않습니다.
- 네이티브 함수 본문, 상속된 Blueprint 그래프, 관련 Task Blueprint 내부 그래프는 자동 확장하지 않습니다. 필요하면 개별 Blueprint 추출 함수를 호출하세요.
- 저장된 BT 자식 순서와 수정 중인 BT 에디터 그래프가 다를 수 있습니다. 둘을 모두 제공하지만 트리를 재빌드해 맞추지 않습니다.
- transient/deprecated 프로퍼티는 제외합니다. 특히 key selector의 런타임 ID·타입·필터 배열은 초기화하거나 추정하지 않습니다.
- 노드의 `stored_error_message`는 저장된 메시지입니다. 새 컴파일 검사 결과가 아닙니다.
- 추가로 읽지 못한 참조·누락·깊이 제한은 `unavailable_information`에 기록합니다.

## 사용한 UE 5.4.4 API

설치된 `C:/Program Files/Epic Games/UE_5.4/Engine/Source`에서 다음 헤더를 직접 확인했습니다.

| 헤더 | 사용한 정보 |
|---|---|
| `Runtime/Engine/Classes/Engine/Blueprint.h` | `GetAllGraphs`, `UbergraphPages`, `FunctionGraphs`, `MacroGraphs` |
| `Runtime/Engine/Classes/EdGraph/EdGraphNode.h`, `EdGraphPin.h` | 노드, 핀 타입·기본값·`LinkedTo`, 분할 핀 |
| `Editor/BlueprintGraph/Classes/K2Node_CallFunction.h`, `K2Node_Event.h` | `FunctionReference`, `GetTargetFunction`, `EventReference` |
| `Runtime/Engine/Classes/Engine/MemberReference.h` | 참조 이름·소유 클래스·self context |
| `Runtime/AIModule/Classes/BehaviorTree/BehaviorTree.h` | 공개 C++ 필드 `RootNode`, `BlackboardAsset`, `BTGraph`, 루트 Decorator |
| `BTCompositeNode.h`, `BTTaskNode.h` (동일 BT 디렉터리) | `Children`, Decorator와 논리 연산, `Services` |
| `BlackboardData.h`, `BehaviorTreeTypes.h` (동일 BT 디렉터리) | 부모 Blackboard, 키 정의, key selector |

UE 5.8의 `BlueprintGraphEditor`나 Python `BlueprintEditorLibrary.list_graphs`에 의존하지 않습니다.

추출 결과의 연결 무결성 검증:

```powershell
python Plugins/AIGraphExporter/Scripts/verify_exports.py Saved/AIExports
```

## 이 프로젝트에서의 검증 결과

2026-10-13, `5.4.4-35576357+++UE5+Release-5.4`, Win64 Development Editor:

- 프로젝트 Editor 타깃 빌드 성공, Commandlet 종료 코드 0.
- `AIC_Ghost`: 그래프 2개, 노드 26개, 핀 125개. 양방향 연결 68개(각 연결의 양끝을 세므로 실제 연결은 34개).
- `BT_Ghost`: Composite 4개, Task 7개, Decorator 3개, Service 0개. Blackboard 키 5개.
- 핀 대상 존재·양방향 연결·핀 방향·실행/데이터 종류, BT 자식 순서·Decorator 인덱스·Blackboard 참조 검증 통과.
- 두 파일의 `unavailable_information`은 빈 배열. 문서에 명시한 런타임/상속/네이티브 본문 등의 제외 범위까지 추출했다는 의미는 아닙니다.
- `Content/BP_Object/AIGhost`의 원본 에셋 5개를 실행 전후 SHA-256 비교: 변경 0개.
- MCP `ue_exec_python`에서 에디터 재시작 후 `unreal.AIGraphExportLibrary.export_ghost_graphs()` 실제 호출 성공. `AIC_Ghost`와 `BT_Ghost` 모두 성공하고 미해결 항목은 0개였습니다. 개별 추출 함수로 `BTTask_FindRandomPatrol`, `BTT_ClearNoise`도 추출했습니다. 에디터를 자동 종료하거나 에셋을 저장하지 않았습니다.

실행 중 처음 발생한 Commandlet 실패는 샌드박스의 외부 DDC 캐시 쓰기 제한 때문이었습니다. 승인된 실행으로 재검증한 결과 추출은 정상 완료됐습니다.
