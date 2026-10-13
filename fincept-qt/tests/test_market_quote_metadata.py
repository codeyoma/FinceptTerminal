"""Fork regression tests for source currency and native quote units (codeyoma/krx-quant#142).

Only Yahoo/pandas I/O is stubbed; the real JSON-producing functions run.
"""
import importlib.util
import math
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch


class Series:
    def __init__(self, values):
        self.values = values
        self.iloc = values
        self.empty = not values


class History:
    empty = False
    columns = ["Open", "High", "Low", "Close", "Volume"]

    def __getitem__(self, key):
        return Series({"Close": [0.987650, 0.987654], "Open": [0.98, 0.981234],
                       "High": [0.99, 0.991234], "Low": [0.97, 0.971234],
                       "Volume": [100, 200]}[key])

    def dropna(self, **kwargs):
        return self

    def __len__(self):
        return 2


class QuoteMetadataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        script = Path(__file__).parents[1] / "scripts" / "yfinance_data.py"
        yf = types.ModuleType("yfinance")
        pd = types.ModuleType("pandas")
        pd.MultiIndex = type("MultiIndex", (), {})
        pd.isna = lambda value: value is None or (isinstance(value, float) and math.isnan(value))
        spec = importlib.util.spec_from_file_location("market_metadata_test_subject", script)
        cls.subject = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, {"yfinance": yf, "pandas": pd}):
            spec.loader.exec_module(cls.subject)

    def test_metadata_distinguishes_price_units_and_preserves_pence(self):
        infos = {
            "AAPL": {"shortName": "Apple", "currency": "USD", "quoteType": "EQUITY"},
            "VOD.L": {"shortName": "Vodafone", "currency": "GBp", "quoteType": "EQUITY"},
            "^GSPC": {"shortName": "S&P", "currency": "USD", "quoteType": "INDEX"},
            "EURUSD=X": {"shortName": "EUR/USD", "currency": "USD", "quoteType": "CURRENCY"},
            "^TNX": {"shortName": "10Y yield", "currency": "USD", "quoteType": "INDEX"},
            "MISSING": {"shortName": "Unknown"},
        }
        with patch.object(self.subject.yf, "Ticker", create=True,
                          side_effect=lambda symbol: types.SimpleNamespace(info=infos[symbol])):
            result = self.subject.get_quote_names(list(infos))
        self.assertEqual(result.get("quote_types"), {
            "AAPL": "EQUITY", "VOD.L": "EQUITY", "^GSPC": "INDEX",
            "EURUSD=X": "CURRENCY", "^TNX": "YIELD", "MISSING": "UNKNOWN"})
        self.assertEqual(result["currencies"].get("VOD.L"), "GBp")
        self.assertNotIn("MISSING", result["currencies"])

    def test_single_quote_retains_source_metadata_and_precision(self):
        ticker = types.SimpleNamespace(
            info={"currency": "CHF", "quoteType": "EQUITY", "previousClose": 0.987650},
            history=lambda **kwargs: History())
        with patch.object(self.subject.yf, "Ticker", create=True, return_value=ticker):
            result = self.subject.get_quote("LOWPRICE.SW")
        self.assertEqual(result.get("currency"), "CHF")
        self.assertEqual(result.get("quote_type"), "EQUITY")
        self.assertEqual(result["price"], 0.987654)
        self.assertAlmostEqual(result["change"], 0.000004)

    def test_batch_quote_preserves_precision_without_guessing_usd(self):
        with patch.object(self.subject.yf, "download", create=True, return_value=History()):
            result = self.subject.get_batch_quotes(["UNRESOLVED"])[0]
        self.assertEqual(result["price"], 0.987654)
        self.assertEqual(result.get("currency", ""), "")
        self.assertEqual(result.get("quote_type"), "UNKNOWN")

    def test_reporting_currency_does_not_default_to_quote_currency(self):
        ticker = types.SimpleNamespace(info={"currency": "USD", "financialCurrency": "KRW"})
        with patch.object(self.subject.yf, "Ticker", create=True, return_value=ticker):
            result = self.subject.get_info("DUAL")
        self.assertEqual(result.get("currency"), "USD")
        self.assertEqual(result.get("financial_currency"), "KRW")
        ticker.info = {"currency": "USD"}
        with patch.object(self.subject.yf, "Ticker", create=True, return_value=ticker):
            result = self.subject.get_info("UNKNOWN_REPORTING")
        self.assertEqual(result.get("financial_currency"), "")


if __name__ == "__main__":
    unittest.main()
