"""Guard against false compatibility passes from extension API replies alone."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from workspace_extensions_smoke import classify


class ExtensionObservationTests(unittest.TestCase):
    def setUp(self):
        self.report = {"mode": "workspace", "first": {"id": 20, "url": "http://127.0.0.1/fixture"},
                       "selected": {"id": 3, "windowId": 11},
                       "update": {"id": 20, "active": True},
                       "before": {"all": [{"id": 20}, {"id": 3}]},
                       "documentRetained": True, "websiteBridge": "undefined",
                       "popup": {"opened": False, "error": "Browser window has no toolbar."},
                       "popupDocument": None, "remainingPopupViews": 0}
        self.rows = [{"event": "extension_observation", "details": {
            "phase": phase, "source_browser_id": 2 if phase == "after_update" else 3,
            "source_url": "http://127.0.0.1/fixture" if phase == "after_update" else "http://127.0.0.1/fixture?saved",
            "content_browser_id": 3, "workspace_ready": True,
            "snapshot": {"activeId": "saved-node"}, "single_visible_active": True,
            "views": [{"browser_id": 2, "visible": False}, {"browser_id": 3, "visible": True}],
        }} for phase in ["before", "after_update", "after_restore", "after_popup"]]

    def test_api_success_without_visible_selection_is_unsupported(self):
        result = classify(self.report, self.rows)
        self.assertEqual(result["activation"], "UNSUPPORTED_FOR_THIS_HOSTING")
        self.assertEqual(result["action_popup"], "UNSUPPORTED_FOR_THIS_HOSTING")

    def test_visible_target_without_model_selection_is_not_a_pass(self):
        after = self.rows[1]["details"]
        after["content_browser_id"] = 2
        after["views"][0]["visible"] = True
        after["views"][1]["visible"] = False
        self.assertEqual(classify(self.report, self.rows)["activation"], "UNSUPPORTED_FOR_THIS_HOSTING")

    def test_missing_native_observation_is_a_test_failure(self):
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            classify(self.report, self.rows[:-1])

    def test_popup_api_success_without_document_is_a_test_failure(self):
        self.report["popup"] = {"opened": True}
        with self.assertRaisesRegex(ValueError, "real isolated"):
            classify(self.report, self.rows)

    def test_popup_context_and_lifecycle_are_required(self):
        self.report["popup"] = {"opened": True}
        self.report["popupDocument"] = {"bridge": "undefined", "visibility": "visible",
            "viewTypeCount": 1, "tabIds": [3], "windowIds": [11]}
        self.assertEqual(classify(self.report, self.rows)["action_popup"], "VERIFIED")
        for field, value in [("bridge", "function"), ("tabIds", [2])]:
            with self.subTest(field=field):
                altered = copy.deepcopy(self.report)
                altered["popupDocument"][field] = value
                with self.assertRaises(ValueError):
                    classify(altered, self.rows)
        self.report["remainingPopupViews"] = 1
        self.assertEqual(classify(self.report, self.rows)["action_popup"], "PARTIAL")
        self.report["remainingPopupViews"] = 0
        self.report["popupDocument"]["visibility"] = "hidden"
        self.assertEqual(classify(self.report, self.rows)["action_popup"], "PARTIAL")

    def test_multiple_visible_pages_fail_the_harness(self):
        self.rows[1]["details"]["single_visible_active"] = False
        with self.assertRaisesRegex(ValueError, "single-visible-active"):
            classify(self.report, self.rows)
