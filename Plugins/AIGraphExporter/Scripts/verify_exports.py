"""Validate real export relationships, not just JSON field presence (stdlib only)."""
import json
import sys
from pathlib import Path


def verify_graphs(graphs):
    pins = {}
    nodes = 0
    for graph in graphs:
        for node in graph['nodes']:
            nodes += 1
            for pin in node['pins']:
                key = (pin['node_path'], pin['pin_id'])
                assert key not in pins, f'duplicate pin identity: {key}'
                pins[key] = pin
    links = 0
    for key, pin in pins.items():
        for link in pin['links']:
            target_key = (link['node_path'], link['pin_id'])
            assert target_key in pins, f'link target missing: {target_key}'
            target = pins[target_key]
            assert any((back['node_path'], back['pin_id']) == key for back in target['links']), f'non-reciprocal link: {key}'
            assert pin['direction'] != target['direction'], f'same-direction link: {key}'
            assert pin['kind'] == target['kind'], f'execution/data mismatch: {key}'
            links += 1
    return {'graphs': len(graphs), 'nodes': nodes, 'pins': len(pins), 'directed_links': links}


def selectors(value):
    if isinstance(value, dict):
        if value.get('struct_type', '').endswith('.BlackboardKeySelector'):
            yield value['SelectedKeyName']['export_text']
        for item in value.values():
            yield from selectors(item)
    elif isinstance(value, list):
        for item in value:
            yield from selectors(item)


def verify_tree(asset):
    board = asset['blackboard']
    keys = set()
    while board:
        keys.update(key['name'] for key in board['local_keys'])
        board = board.get('parent')
    counts = {'composite': 0, 'task': 0, 'decorator': 0, 'service': 0}
    selected = set()

    def walk(node):
        assert not node.get('reference_only'), 'Repeated node requires manual review'
        counts[node['kind']] += 1
        for name in selectors(node['settings']):
            if name != 'None':
                assert name in keys, f'unknown Blackboard key selector: {name}'
                selected.add(name)
        for service in node['services']:
            assert service['kind'] == 'service'
            walk(service)
        for index, child in enumerate(node.get('children', [])):
            assert child['child_index'] == index
            assert child['node'] is not None
            for decorator in child['decorators']:
                assert decorator['kind'] == 'decorator'
                walk(decorator)
            for op in child['decorator_logic']:
                if op['operation'] == 'Test':
                    assert 0 <= op['number'] < len(child['decorators'])
            walk(child['node'])

    assert asset['root']['kind'] == 'composite'
    walk(asset['root'])
    for decorator in asset['root_decorators']:
        walk(decorator)
    return {**counts, 'blackboard_keys': sorted(keys), 'selected_keys': sorted(selected)}


def main():
    directory = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('Saved/AIExports')
    report = {}
    bp_files = list(directory.glob('AIC_Ghost_*_blueprint.json'))
    bt_files = list(directory.glob('BT_Ghost_*_behavior_tree.json'))
    assert len(bp_files) == len(bt_files) == 1, 'Expected one AIC_Ghost and one BT_Ghost export'
    for path in bp_files + bt_files:
        document = json.loads(path.read_text(encoding='utf-8'))
        assert document['read_only']
        assert document['package_dirty_before'] == document['package_dirty_after']
        asset = document['asset']
        if path in bp_files:
            summary = verify_graphs(asset['graphs'])
            assert any(graph['kind'] == 'event' for graph in asset['graphs'])
        else:
            summary = verify_tree(asset)
            summary['editor_graph'] = verify_graphs([asset['editor_graph']])
        summary['unavailable_information'] = document['unavailable_information']
        report[path.name] = summary
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
