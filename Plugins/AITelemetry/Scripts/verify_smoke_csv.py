"""Check the actual C++ smoke output with Python's CSV parser, including quoting."""
import csv
import sys
from pathlib import Path


def read(path):
    with path.open(encoding='utf-8', newline='') as stream:
        reader = csv.DictReader(stream)
        rows = list(reader)
        assert rows, f'No data rows: {path}'
        for row in rows:
            assert None not in row, f'Extra columns: {path}'
            assert all(value is not None for value in row.values()), f'Missing columns: {path}'
            assert row['run_kind'] == 'smoke', 'This validator is for smoke runs, not tester data'
        return rows


def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('Saved/AITelemetry')
    if (root / 'samples.csv').is_file():
        directory = root
    else:
        directories = []
        for path in root.glob('*/events.csv'):
            with path.open(encoding='utf-8', newline='') as stream:
                first = next(csv.DictReader(stream), {})
            if first.get('run_kind') == 'smoke':
                directories.append(path.parent)
        assert directories, 'No smoke output found'
        directory = max(directories, key=lambda path: path.stat().st_mtime)
    assert {p.name for p in directory.iterdir()} == {'samples.csv', 'events.csv'}
    samples, events = read(directory / 'samples.csv'), read(directory / 'events.csv')
    selection = next(row for row in events if row['event_kind'] == 'PatrolTargetSelected')
    assert selection['patrol_mode'] == 'zone' and selection['patrol_zone'] == 'smoke_zone'
    assert float(selection['target_x_cm']) == 600 and float(selection['target_y_cm']) == 700
    assert float(selection['target_z_cm']) == 300 and float(selection['contact_gap_s']) == 45
    sample = next(row for row in samples if row['active_node'] == 'SyntheticSmokeNode')
    missing = next(row for row in samples if row['active_node'] == 'SyntheticMissingPlayer')
    assert sample['state'] == 'Patrol'
    assert float(sample['ghost_x_cm']) == 123
    assert float(sample['ghost_y_cm']) == -456
    assert missing['player_x_cm'] == missing['player_y_cm'] == missing['player_z_cm'] == ''
    assert events[0]['event_kind'] == 'SessionStart'
    assert events[-1]['event_kind'] == 'SessionEnd'
    encounter = next(row for row in events if row['reason'].startswith('synthetic,quoted'))
    assert encounter['reason'] == 'synthetic,quoted "smoke"\nmultiline'
    assert encounter['encounter_count'] == '1'
    callbacks = [row for row in events if row['reason'] == 'perception_callback']
    assert len([row for row in callbacks if row['event_kind'] == 'SenseSuccess']) == 4
    assert len([row for row in callbacks if row['event_kind'] == 'SenseLost']) == 2
    starts = [row for row in callbacks if row['event_kind'] == 'EncounterStart']
    ends = [row for row in callbacks if row['event_kind'] == 'EncounterEnd']
    assert [row['encounter_count'] for row in starts] == ['1', '2']
    assert len(ends) == 1
    assert all(row['run_id'] == samples[0]['run_id'] for row in samples + events)
    print(f'CSV smoke validation passed: {directory}')


if __name__ == '__main__':
    main()
