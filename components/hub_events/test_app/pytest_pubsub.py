from pytest_embedded_idf.dut import IdfDut
from pytest_embedded_idf.utils import idf_parametrize


@idf_parametrize('target', ['esp32s31'], indirect=['target'])
def test_pubsub(dut: IdfDut) -> None:
    dut.expect('All tests passed', timeout=10)
    dut.expect_exact('hub_events_tests: passed')
