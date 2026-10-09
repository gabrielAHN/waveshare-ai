from test_provider import _load, PLUGIN_DIR


def test_dashboard_exposes_only_usage_and_bots():
    api = _load('current_dashboard_contract', PLUGIN_DIR / 'dashboard' / 'plugin_api.py')
    assert {r.path for r in api.router.routes} == {'/usage', '/bots'}
