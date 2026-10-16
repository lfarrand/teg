import copy
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from arc_detector.fpga_constraints import emit_research_pdc


def netlist():
    widths = (("clk", 1, "input"), ("rst", 1, "input"), ("in_valid", 1, "input"),
              ("in_sample", 24, "input"), ("in_ready", 1, "output"),
              ("candidate_valid", 1, "output"), ("candidate_latched", 1, "output"),
              ("candidate_index", 64, "output"), ("protocol_fault", 1, "output"))
    offset, ports = 2, {}
    for name, width, direction in widths:
        ports[name] = {"direction": direction, "bits": list(range(offset, offset + width))}
        offset += width
    return {"modules": {"fast": {"attributes": {"top": "00000001"}, "ports": ports}}}


def mapped(directory, design=None):
    path = Path(directory) / "mapped.json"
    path.write_text(json.dumps(netlist() if design is None else design), encoding="utf-8")
    return path


class ResearchConstraintsTests(unittest.TestCase):
    def test_full_wrapper_uses_95_unique_pads_and_explicit_research_denials(self):
        with tempfile.TemporaryDirectory() as directory:
            source = mapped(directory)
            pdc = Path(directory) / "research.pdc"
            result = emit_research_pdc(source, pdc)
            provenance = json.loads(Path(result["provenance"]).read_text())
            text = pdc.read_text()
            self.assertEqual(result["port_count"], 95)
            self.assertFalse(result["hardware_programming_authorized"])
            self.assertEqual(len(provenance["bindings"]), 95)
            self.assertEqual(len({row["port"] for row in provenance["bindings"]}), 95)
            self.assertEqual(len({row["pin"] for row in provenance["bindings"]}), 95)
            self.assertEqual(provenance["bindings"][0], {"port": "clk", "pin": "F6", "io_type": "LVCMOS18"})
            self.assertEqual(provenance["part"], "LFD2NX-40-9BG256C")
            self.assertEqual(provenance["package"], "CABGA256")
            self.assertEqual(provenance["bank_supply_assumption_v"], 1.8)
            self.assertFalse(provenance["hardware_programming_authorized"])
            self.assertFalse(provenance["device_fit_proven"])
            self.assertFalse(provenance["timing_closure_proven"])
            self.assertIn("NOT A BOARD PINOUT - NOT FOR PROGRAMMING HARDWARE", text)
            self.assertIn("8d8174509356fa5bde12db614c230f7c425f9d2b", provenance["pin_database_url"])
            self.assertEqual(provenance["pin_database_sha256"],
                             "a0f83c62022c3361b60a2373afa1052543e3ab2f7456de144609c6ffd5f52496")
            self.assertEqual(provenance["pdc_sha256"], hashlib.sha256(pdc.read_bytes()).hexdigest())
            self.assertEqual(provenance["netlist_sha256"], hashlib.sha256(source.read_bytes()).hexdigest())
            self.assertEqual(text.count("ldc_set_location "), 95)
            self.assertEqual(text.count("ldc_set_port "), 95)
            self.assertEqual(sum(row["io_type"] == "LVCMOS18" for row in provenance["bindings"]), 85)
            self.assertEqual(sum(row["io_type"] == "LVCMOS18H" for row in provenance["bindings"]), 10)
            self.assertTrue({row["pin"] for row in provenance["bindings"]}.isdisjoint(
                {"F14", "E14", "F11", "E11", "D10", "B16", "C12", "D11", "-"}))

    def test_actual_ports_offsets_and_ascending_ranges_are_enumerated(self):
        design = netlist()
        design["modules"]["fast"]["ports"] = {
            "clk": {"direction": "input", "bits": [2]},
            "up": {"direction": "output", "bits": [3, 4, 5], "offset": 4, "upto": 1},
            "down": {"direction": "input", "bits": [6, 7], "offset": 7},
        }
        with tempfile.TemporaryDirectory() as directory:
            result = emit_research_pdc(mapped(directory, design), Path(directory) / "research.pdc")
            rows = json.loads(Path(result["provenance"]).read_text())["bindings"]
            self.assertEqual([row["port"] for row in rows],
                             ["clk", "down[7]", "down[8]", "up[6]", "up[5]", "up[4]"])
            self.assertEqual(result["port_count"], 6)

    def test_invalid_netlists_and_capacity_are_rejected_before_publication(self):
        original = netlist()
        cases = []
        for name in ("no-top", "two-tops", "no-clock", "output-clock", "unsafe-name", "too-wide", "bad-bits", "bad-offset"):
            design = copy.deepcopy(original)
            top = design["modules"]["fast"]
            if name == "no-top":
                top["attributes"] = {}
            elif name == "two-tops":
                design["modules"]["other"] = copy.deepcopy(top)
            elif name == "no-clock":
                del top["ports"]["clk"]
            elif name == "output-clock":
                top["ports"]["clk"]["direction"] = "output"
            elif name == "unsafe-name":
                top["ports"]["bad} [get_ports clk]"] = {"direction": "input", "bits": [123]}
            elif name == "too-wide":
                top["ports"]["extra"] = {"direction": "input", "bits": [123]}
            elif name == "bad-bits":
                top["ports"]["clk"]["bits"] = [True]
            else:
                top["ports"]["in_sample"]["offset"] = True
            cases.append((name, design))
        for name, design in cases:
            with self.subTest(case=name), tempfile.TemporaryDirectory() as directory:
                source = mapped(directory, design)
                with self.assertRaises(ValueError):
                    emit_research_pdc(source, Path(directory) / "research.pdc")
                self.assertEqual([path.name for path in Path(directory).iterdir()], ["mapped.json"])

    def test_deterministic_outputs_are_independent_of_json_port_key_order(self):
        design = netlist()
        reversed_design = copy.deepcopy(design)
        reversed_design["modules"]["fast"]["ports"] = dict(reversed(list(design["modules"]["fast"]["ports"].items())))
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            emit_research_pdc(mapped(first, design), Path(first) / "research.pdc")
            emit_research_pdc(mapped(second, reversed_design), Path(second) / "research.pdc")
            self.assertEqual((Path(first) / "research.pdc").read_bytes(), (Path(second) / "research.pdc").read_bytes())

    def test_collisions_and_failed_second_link_preserve_existing_artifacts(self):
        for occupied in ("research.pdc", "research.provenance.json"):
            with self.subTest(occupied=occupied), tempfile.TemporaryDirectory() as directory:
                source = mapped(directory)
                existing = Path(directory) / occupied
                existing.write_bytes(b"owned artifact")
                with self.assertRaises(FileExistsError):
                    emit_research_pdc(source, Path(directory) / "research.pdc")
                self.assertEqual(existing.read_bytes(), b"owned artifact")
                self.assertEqual({path.name for path in Path(directory).iterdir()}, {"mapped.json", occupied})
        with tempfile.TemporaryDirectory() as directory:
            source, real_link, calls = mapped(directory), os.link, []
            def fail_second(stage, destination):
                calls.append(str(destination))
                if len(calls) == 2:
                    raise PermissionError("injected publication failure")
                return real_link(stage, destination)
            with patch("arc_detector.fpga_constraints.os.link", side_effect=fail_second):
                with self.assertRaises(PermissionError):
                    emit_research_pdc(source, Path(directory) / "research.pdc")
            self.assertEqual(len(calls), 2)
            self.assertEqual([path.name for path in Path(directory).iterdir()], ["mapped.json"])

    def test_netlist_size_and_destination_guards_leave_no_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            source = mapped(directory)
            with self.assertRaises(ValueError):
                emit_research_pdc(source, Path(directory) / "research.txt")
            with patch("arc_detector.fpga_constraints._MAX_NETLIST_BYTES", 1):
                with self.assertRaises(ValueError):
                    emit_research_pdc(source, Path(directory) / "research.pdc")
            self.assertEqual([path.name for path in Path(directory).iterdir()], ["mapped.json"])


if __name__ == "__main__":
    unittest.main()
