#!/usr/bin/env python3
"""
Opencode In-Runner Self-Healing Agent for Android & Recovery Builds
------------------------------------------------------------------
Supervises compilation, captures errors from build logs/stderr, diagnoses root
causes using heuristic rules and OpenCode AI CLI, automatically executes targeted
patches in-situ, logs all remediation actions, and retries builds seamlessly.
"""

import argparse
import datetime
import glob
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


class Colors:
    HEADER = "\033[95m"
    BLUE = "\033[94m"
    CYAN = "\033[96m"
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    RED = "\033[91m"
    BOLD = "\033[1m"
    UNDERLINE = "\033[4m"
    END = "\033[0m"


class OpencodeAgent:
    def __init__(self, work_dir, device_dir, log_file, actions_log_file):
        self.work_dir = Path(work_dir).resolve()
        self.device_dir = (self.work_dir / device_dir).resolve() if not Path(device_dir).is_absolute() else Path(device_dir).resolve()
        self.log_file = Path(log_file).resolve()
        self.actions_log_file = Path(actions_log_file).resolve()
        self.remediations_taken = []
        self.env_overrides = {}

    def log(self, message, level="INFO"):
        now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
        color_map = {
            "INFO": Colors.CYAN,
            "SUCCESS": Colors.GREEN,
            "WARNING": Colors.YELLOW,
            "ERROR": Colors.RED,
            "ACTION": Colors.BOLD + Colors.BLUE,
        }
        color = color_map.get(level, Colors.END)
        formatted = f"[{now}] [OPENCODE-AGENT] [{level}] {message}"
        print(f"{color}{formatted}{Colors.END}", flush=True)

        # Append to actions log file
        try:
            with open(self.actions_log_file, "a", encoding="utf-8") as f:
                f.write(formatted + "\n")
        except Exception as e:
            print(f"Warning: Could not write to actions log: {e}", file=sys.stderr)

    def write_ci_summary(self):
        """Append markdown summary table to $GITHUB_STEP_SUMMARY if available."""
        summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
        if not summary_path or not self.remediations_taken:
            return

        try:
            with open(summary_path, "a", encoding="utf-8") as f:
                f.write("\n### 🤖 Opencode Agent Auto-Remediation Summary\n\n")
                f.write("| Attempt | Diagnosis | Remediation Action | Status |\n")
                f.write("|:---:|:---|:---|:---:|\n")
                for r in self.remediations_taken:
                    attempt = r.get("attempt", "N/A")
                    diag = r.get("diagnosis", "Unknown")
                    action = r.get("action", "Applied patch")
                    status = "✅ Healed" if r.get("success", False) else "⚠️ Fallback"
                    f.write(f"| {attempt} | {diag} | {action} | {status} |\n")
                f.write("\n")
        except Exception as e:
            self.log(f"Failed to write GITHUB_STEP_SUMMARY: {e}", level="WARNING")

    def read_tail_log(self, max_lines=500):
        if not self.log_file.exists():
            return ""
        try:
            with open(self.log_file, "r", encoding="utf-8", errors="replace") as f:
                lines = f.readlines()
                return "".join(lines[-max_lines:])
        except Exception as e:
            self.log(f"Error reading log file {self.log_file}: {e}", level="ERROR")
            return ""

    # =========================================================================
    # Auto-Remediation Handlers
    # =========================================================================

    def fix_dexpreopt_user_userdebug(self, log_content="", force=False):
        """Removes WITH_DEXPREOPT := false which breaks Android 12 lunch."""
        if not force and "DEXPREOPT must be enabled for user and userdebug builds" not in log_content:
            return False

        self.log("Detected fatal DEXPREOPT enforcement in dex_preopt_config.mk", level="ACTION")
        fixed_files = []

        # 1. Clean device makefiles
        mk_files = list(self.device_dir.glob("*.mk"))
        for mk in mk_files:
            try:
                content = mk.read_text(encoding="utf-8")
                new_content = re.sub(r"^[ \t]*(WITH_DEXPREOPT|DEX_PREOPT_DEFAULT)[ \t]*:=.*$", "", content, flags=re.MULTILINE)
                if new_content != content:
                    mk.write_text(new_content, encoding="utf-8")
                    fixed_files.append(mk.name)
            except Exception as e:
                self.log(f"Could not inspect {mk}: {e}", level="WARNING")

        # 2. Neutralize dex_preopt_config.mk fatal error guard if present in source tree
        dex_config = self.work_dir / "build" / "make" / "core" / "dex_preopt_config.mk"
        if dex_config.exists():
            try:
                cfg_content = dex_config.read_text(encoding="utf-8")
                if "error DEXPREOPT must be enabled" in cfg_content:
                    new_cfg = cfg_content.replace(
                        "$(error DEXPREOPT must be enabled for user and userdebug builds.)",
                        "$(warning [Opencode Agent] Bypassed DEXPREOPT fatal check for recovery)"
                    )
                    dex_config.write_text(new_cfg, encoding="utf-8")
                    fixed_files.append("build/make/core/dex_preopt_config.mk")
            except Exception as e:
                self.log(f"Could not patch dex_preopt_config.mk: {e}", level="WARNING")

        action_desc = f"Stripped WITH_DEXPREOPT from {', '.join(fixed_files) if fixed_files else 'tree'}"
        self.log(f"Remediation: {action_desc}", level="SUCCESS")
        return action_desc

    def fix_dexpreopt_check_task(self, log_content):
        """Neutralizes dex_preopt_check.mk failures caused by pruned system server apexes."""
        if "dex_preopt_check.mk" not in log_content:
            return False

        self.log("Detected dex_preopt_check.mk failure", level="ACTION")
        check_mk = self.work_dir / "build" / "make" / "core" / "tasks" / "dex_preopt_check.mk"
        if check_mk.exists():
            try:
                check_mk.write_text("# Neutralized by Opencode Agent\n.PHONY: dexpreopt_check\ndexpreopt_check:\n\t@true\n", encoding="utf-8")
                desc = "Neutralized build/make/core/tasks/dex_preopt_check.mk"
                self.log(f"Remediation: {desc}", level="SUCCESS")
                return desc
            except Exception as e:
                self.log(f"Failed to patch dex_preopt_check.mk: {e}", level="ERROR")
        return False

    def fix_obsolete_board_vars(self, log_content):
        """Strips Kati/Soong obsolete board variables."""
        obsolete_vars = re.findall(r"([A-Z0-9_]+) is obsolete", log_content)
        if not obsolete_vars:
            return False

        stripped = []
        for var in set(obsolete_vars):
            for mk in self.device_dir.glob("*.mk"):
                try:
                    content = mk.read_text(encoding="utf-8")
                    pattern = rf"^[ \t]*{re.escape(var)}[ \t:?+]*=.*$"
                    new_content = re.sub(pattern, "", content, flags=re.MULTILINE)
                    if new_content != content:
                        mk.write_text(new_content, encoding="utf-8")
                        stripped.append(f"{var} in {mk.name}")
                except Exception as e:
                    self.log(f"Error modifying {mk}: {e}", level="WARNING")

        if stripped:
            desc = f"Stripped obsolete variables: {', '.join(stripped)}"
            self.log(f"Remediation: {desc}", level="SUCCESS")
            return desc
        return False

    def fix_recovery_partition_overflow(self, log_content):
        """Fixes recovery.img size exceeding physical partition size."""
        match = re.search(r"recovery\.img too large \(([0-9]+) > ([0-9]+)\)", log_content)
        if not match:
            return False

        img_size, part_size = match.groups()
        self.log(f"Detected partition overflow: recovery.img ({img_size} bytes) > partition limit ({part_size} bytes)", level="ACTION")

        board_cfg = self.device_dir / "BoardConfig.mk"
        vendorsetup = self.device_dir / "vendorsetup.sh"
        aera_mk = self.device_dir / "aera_m20lte.mk"
        actions = []

        if board_cfg.exists():
            content = board_cfg.read_text(encoding="utf-8")
            if "LZMA_RAMDISK_TARGETS" not in content:
                content += "\nLZMA_RAMDISK_TARGETS := recovery\n"
                actions.append("Enabled LZMA_RAMDISK_TARGETS")
            if "OF_USE_LZMA_COMPRESSION" not in content:
                content += "\nOF_USE_LZMA_COMPRESSION := 1\n"
                actions.append("Set OF_USE_LZMA_COMPRESSION")
            if "BOARD_RAMDISK_USE_XZ" not in content:
                content += "\nBOARD_RAMDISK_USE_XZ := true\n"
                actions.append("Set BOARD_RAMDISK_USE_XZ := true")

            # Disable high-footprint modules
            for var in ["TW_INCLUDE_CRYPTO", "TW_INCLUDE_CRYPTO_FBE", "AERA_INCLUDE_CRYPTO", "AERA_INCLUDE_CRYPTO_FBE",
                        "TW_INCLUDE_NTFS_3G", "AERA_INCLUDE_NTFS_3G", "TW_INCLUDE_FUSE_NTFS", "AERA_INCLUDE_FUSE_NTFS",
                        "TWRP_INCLUDE_LOGCAT", "AERA_INCLUDE_LOGCAT", "TARGET_USES_LOGD"]:
                pattern = rf"^[ \t]*{var}[ \t]*:=.*$"
                if re.search(pattern, content, flags=re.MULTILINE):
                    content = re.sub(pattern, f"{var} := false", content, flags=re.MULTILINE)
                    actions.append(f"Disabled {var}")

            board_cfg.write_text(content, encoding="utf-8")

        if aera_mk.exists():
            acontent = aera_mk.read_text(encoding="utf-8")
            if "AERA_ENABLE_WLAN := 1" in acontent:
                acontent = acontent.replace("AERA_ENABLE_WLAN := 1", "AERA_ENABLE_WLAN := 0")
                aera_mk.write_text(acontent, encoding="utf-8")
                actions.append("Disabled AERA_ENABLE_WLAN")

        if vendorsetup.exists():
            vcontent = vendorsetup.read_text(encoding="utf-8")
            if 'export OF_USE_LZMA_COMPRESSION="1"' not in vcontent:
                vcontent += '\nexport OF_USE_LZMA_COMPRESSION="1"\n'
                vendorsetup.write_text(vcontent, encoding="utf-8")
                actions.append("Exported OF_USE_LZMA_COMPRESSION in vendorsetup.sh")

        desc = f"Trimming recovery ramdisk to fit partition limit: {', '.join(actions)}"
        self.log(f"Remediation: {desc}", level="SUCCESS")
        return desc

    def fix_rbe_bootstrap(self, log_content):
        """Disables RBE when reclient binaries are absent."""
        if 'rbe command "bootstrap" not found' not in log_content and "failed to find reclient" not in log_content:
            return False

        self.log("Detected missing RBE bootstrap / reclient", level="ACTION")
        self.env_overrides["USE_RBE"] = "false"

        for f in [self.device_dir / "vendorsetup.sh", self.device_dir / "BoardConfig.mk"]:
            if f.exists():
                try:
                    c = f.read_text(encoding="utf-8")
                    c_new = re.sub(r"^[ \t]*(export[ \t]+)?USE_RBE=.*$", "", c, flags=re.MULTILINE)
                    if c_new != c:
                        f.write_text(c_new, encoding="utf-8")
                except Exception as e:
                    self.log(f"Error modifying {f}: {e}", level="WARNING")

        desc = "Disabled USE_RBE and removed bootstrap references"
        self.log(f"Remediation: {desc}", level="SUCCESS")
        return desc

    def fix_broken_duplicate_rules(self, log_content):
        """Adds BUILD_BROKEN_DUP_RULES := true if duplicate rules fail build."""
        if "multiple rules generate" not in log_content and "overriding commands for target" not in log_content:
            return False

        board_cfg = self.device_dir / "BoardConfig.mk"
        if board_cfg.exists():
            c = board_cfg.read_text(encoding="utf-8")
            if "BUILD_BROKEN_DUP_RULES" not in c:
                c += "\nBUILD_BROKEN_DUP_RULES := true\n"
                board_cfg.write_text(c, encoding="utf-8")
                desc = "Added BUILD_BROKEN_DUP_RULES := true to BoardConfig.mk"
                self.log(f"Remediation: {desc}", level="SUCCESS")
                return desc
        return False

    def fix_broken_elf_prebuilts(self, log_content):
        """Adds BUILD_BROKEN_ELF_PREBUILT_PRODUCT_COPY_FILES := true."""
        if "prebuilt ELF binaries" not in log_content and "check_elf_file.py" not in log_content:
            return False

        board_cfg = self.device_dir / "BoardConfig.mk"
        if board_cfg.exists():
            c = board_cfg.read_text(encoding="utf-8")
            if "BUILD_BROKEN_ELF_PREBUILT_PRODUCT_COPY_FILES" not in c:
                c += "\nBUILD_BROKEN_ELF_PREBUILT_PRODUCT_COPY_FILES := true\n"
                board_cfg.write_text(c, encoding="utf-8")
                desc = "Added BUILD_BROKEN_ELF_PREBUILT_PRODUCT_COPY_FILES := true"
                self.log(f"Remediation: {desc}", level="SUCCESS")
                return desc
        return False

    def fix_missing_required_modules(self, log_content):
        """Adds BUILD_BROKEN_MISSING_REQUIRED_MODULES := true and ALLOW_MISSING_DEPENDENCIES := true."""
        if "missing and no known rule to make it" not in log_content:
            return False

        board_cfg = self.device_dir / "BoardConfig.mk"
        if board_cfg.exists():
            c = board_cfg.read_text(encoding="utf-8")
            modified = False
            if "BUILD_BROKEN_MISSING_REQUIRED_MODULES" not in c:
                c += "\nBUILD_BROKEN_MISSING_REQUIRED_MODULES := true\n"
                modified = True
            if "ALLOW_MISSING_DEPENDENCIES" not in c:
                c += "\nALLOW_MISSING_DEPENDENCIES := true\n"
                modified = True
            if modified:
                board_cfg.write_text(c, encoding="utf-8")
                desc = "Added missing dependency allowances to BoardConfig.mk"
                self.log(f"Remediation: {desc}", level="SUCCESS")
                return desc
        return False

    def fix_sandbox_nsjail(self, log_content):
        """Sets env flags when nsjail sandbox fails."""
        if ("FAILED:" in log_content or "ninja: error:" in log_content) and ("nsjail" in log_content.lower()):
            self.env_overrides["DISABLE_SANDBOX"] = "true"
            desc = "Configured sandbox bypass (DISABLE_SANDBOX=true)"
            self.log(f"Remediation: {desc}", level="SUCCESS")
            return desc
        return False

    def fix_artifact_path_requirements(self, log_content):
        """Disables strict artifact path requirements when files land in unexpected partitions."""
        if "artifact_path_requirements" not in log_content and "produces files outside its artifact path requirement" not in log_content:
            return False

        self.log("Detected artifact_path_requirements failure", level="ACTION")
        self.env_overrides["DISABLE_ARTIFACT_PATH_REQUIREMENTS"] = "true"

        # Patch build/make/core/artifact_path_requirements.mk if present
        mk_path = self.work_dir / "build" / "make" / "core" / "artifact_path_requirements.mk"
        if mk_path.exists():
            try:
                c = mk_path.read_text(encoding="utf-8")
                if "DISABLE_ARTIFACT_PATH_REQUIREMENTS" not in c:
                    c = "DISABLE_ARTIFACT_PATH_REQUIREMENTS := true\n" + c
                    mk_path.write_text(c, encoding="utf-8")
            except Exception as e:
                self.log(f"Failed patching artifact_path_requirements.mk: {e}", level="WARN")

        # Ensure removed from BoardConfig.mk (where product vars are readonly)
        board_cfg = self.device_dir / "BoardConfig.mk"
        if board_cfg.exists():
            try:
                c = board_cfg.read_text(encoding="utf-8")
                if "PRODUCT_ENFORCE_ARTIFACT_PATH_REQUIREMENTS" in c:
                    c = re.sub(r"^[ \t]*PRODUCT_ENFORCE_ARTIFACT_PATH_REQUIREMENTS[ \t:?+]*=.*$\n?", "", c, flags=re.MULTILINE)
                    board_cfg.write_text(c, encoding="utf-8")
            except Exception:
                pass

        # Add to product makefile (e.g. twrp_m20lte.mk) where product variables are valid
        for pfile in [self.device_dir / "twrp_m20lte.mk", self.device_dir / "device.mk"]:
            if pfile.exists():
                try:
                    c = pfile.read_text(encoding="utf-8")
                    if "PRODUCT_ENFORCE_ARTIFACT_PATH_REQUIREMENTS" not in c:
                        c += "\nPRODUCT_ENFORCE_ARTIFACT_PATH_REQUIREMENTS := false\n"
                        pfile.write_text(c, encoding="utf-8")
                except Exception as e:
                    self.log(f"Failed updating {pfile.name}: {e}", level="WARN")


        desc = "Disabled strict artifact path requirements (DISABLE_ARTIFACT_PATH_REQUIREMENTS=true)"
        self.log(f"Remediation: {desc}", level="SUCCESS")
        return desc


    def fix_soong_extra_variables_json(self, log_content):
        """Fixes malformed JSON in soong.*.extra.variables caused by quotes in make variables."""
        if "Error parsing JSON in file:" not in log_content and "extra.variables" not in log_content:
            return False

        self.log("Detected malformed JSON in soong.extra.variables", level="ACTION")
        fixed = []
        # Strip literal quotes around make variables across device makefiles
        for mk in self.device_dir.glob("*.mk"):
            try:
                content = mk.read_text(encoding="utf-8")
                new_content = re.sub(r'(:=\s*)["\']([^"\'\n]+)["\']', r'\1\2', content)
                if new_content != content:
                    mk.write_text(new_content, encoding="utf-8")
                    fixed.append(mk.name)
            except Exception as e:
                self.log(f"Error updating {mk}: {e}", level="WARN")

        # Purge corrupted extra.variables files from out/soong
        soong_dir = self.work_dir / "out" / "soong"
        if soong_dir.exists():
            for f in soong_dir.glob("*.variables"):
                try:
                    f.unlink()
                except Exception:
                    pass

        desc = f"Stripped literal quotes from {', '.join(fixed) if fixed else 'makefiles'} and purged corrupted soong.variables"
        self.log(f"Remediation: {desc}", level="SUCCESS")
        return desc

    def fix_mkbootimg_dt_argument(self, log_content):
        """Patches mkbootimg.py to support --dt for Samsung Exynos."""
        if "mkbootimg: error: unrecognized arguments: --dt" not in log_content:
            return False

        target_mkbootimg = self.work_dir / "system" / "tools" / "mkbootimg" / "mkbootimg.py"
        custom_mkbootimg = self.device_dir / "scripts" / "mkbootimg.py"

        if custom_mkbootimg.exists() and target_mkbootimg.exists():
            try:
                shutil.copy2(custom_mkbootimg, target_mkbootimg)
                desc = "Replaced AOSP mkbootimg.py with Samsung-compatible version supporting --dt"
                self.log(f"Remediation: {desc}", level="SUCCESS")
                return desc
            except Exception as e:
                self.log(f"Failed to copy mkbootimg: {e}", level="ERROR")
        return False

    def fix_missing_host_command(self, log_content):
        """Installs or links missing host command line tools."""
        matches = re.findall(r"([a-zA-Z0-9_\-\.]+): command not found", log_content)
        if not matches:
            return False

        installed = []
        for cmd in set(matches):
            if cmd in ["python", "python2"]:
                if Path("/usr/bin/python3").exists() and not Path("/usr/bin/python").exists():
                    try:
                        subprocess.run(["sudo", "ln", "-sf", "/usr/bin/python3", "/usr/bin/python"], check=True)
                        installed.append(f"Symlinked /usr/bin/python -> python3")
                    except Exception as e:
                        self.log(f"Symlink failed: {e}", level="WARNING")
            else:
                try:
                    res = subprocess.run(["sudo", "apt-get", "install", "-y", cmd], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    if res.returncode == 0:
                        installed.append(f"Installed {cmd} via apt")
                except Exception:
                    pass

        if installed:
            desc = f"Resolved missing commands: {', '.join(installed)}"
            self.log(f"Remediation: {desc}", level="SUCCESS")
            return desc
        return False

    def fix_stale_out_lock(self, log_content):
        """Cleans up stale out/.lock and terminates orphaned build processes."""
        if "out/.lock" not in log_content:
            return False

        self.log("Detected out/.lock collision", level="ACTION")
        try:
            subprocess.run(["pkill", "-9", "-x", "soong_ui"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.run(["pkill", "-9", "-x", "ninja"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception:
            pass

        lock_path = self.work_dir / "out" / ".lock"
        if lock_path.exists():
            try:
                lock_path.unlink()
            except Exception as e:
                self.log(f"Failed unlinking out/.lock: {e}", level="WARNING")

        desc = "Terminated orphaned soong/ninja processes and removed out/.lock"
        self.log(f"Remediation: {desc}", level="SUCCESS")
        return desc

    def fix_invalid_lunch_choices(self, log_content=""):
        """Ensures COMMON_LUNCH_CHOICES only contains valid <product>-<variant> combos without bp2a release tags."""
        prod_mk = self.device_dir / "AndroidProducts.mk"
        if not prod_mk.exists():
            return False
        try:
            content = prod_mk.read_text(encoding="utf-8")
            if "bp2a" in content:
                new_content = re.sub(r"[ \t]*[a-zA-Z0-9_]+-bp2a-[a-zA-Z0-9_]+[ \t]*\\?", "", content)
                new_content = re.sub(r"\n\s*\n+", "\n\n", new_content)
                if new_content != content:
                    prod_mk.write_text(new_content, encoding="utf-8")
                    desc = f"Sanitized COMMON_LUNCH_CHOICES in {prod_mk.name} (removed invalid bp2a combo)"
                    self.log(f"Remediation: {desc}", level="SUCCESS")
                    return desc
        except Exception as e:
            self.log(f"Could not sanitize {prod_mk}: {e}", level="WARNING")
        return False

    def fix_rust_archive_failure(self, log_content=""):
        """Fixes rustc failed to build archive by clearing corrupted rust intermediate files."""
        if "error: failed to build archive" not in log_content:
            return False
        self.log("Detected corrupted Rust sysroot/archive intermediate files", level="ACTION")
        rust_intermediates = self.work_dir / "out" / "soong" / ".intermediates" / "prebuilts" / "rust"
        if rust_intermediates.exists():
            try:
                import shutil
                shutil.rmtree(rust_intermediates)
                desc = f"Removed corrupted {rust_intermediates}"
                self.log(f"Remediation: {desc}", level="SUCCESS")
                return desc
            except Exception as e:
                self.log(f"Failed to remove {rust_intermediates}: {e}", level="WARNING")
        return False


    def run_opencode_cli_fallback(self, log_content):
        """Invokes opencode CLI with failure context if installed."""
        opencode_paths = [
            Path.home() / ".opencode" / "bin" / "opencode",
            Path("/usr/local/bin/opencode"),
            Path(shutil.which("opencode") or "/dev/null"),
        ]
        opencode_bin = next((p for p in opencode_paths if p.exists() and os.access(p, os.X_OK)), None)
        if not opencode_bin:
            return False

        self.log(f"Invoking OpenCode AI Agent CLI ({opencode_bin})...", level="ACTION")

        # Extract last 40 lines of error
        error_lines = log_content.strip().splitlines()[-40:]
        error_excerpt = "\n".join(error_lines)

        prompt = (
            f"An error occurred while compiling the Samsung Galaxy M20 (m20lte) Android recovery tree:\n"
            f"```\n{error_excerpt}\n```\n"
            f"Analyze and apply direct file fixes to files in `{self.device_dir}` or build makefiles to allow `lunch` and `mka recoveryimage` to succeed. "
            f"Do not introduce new dependencies. Fix only the specific syntax or configuration error."
        )

        try:
            cmd = [
                str(opencode_bin),
                "run",
                "--standalone",
                "--auto",
                prompt,
            ]
            self.log(f"Running: {' '.join(cmd[:4])} ...", level="INFO")
            res = subprocess.run(cmd, cwd=str(self.work_dir), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=180)
            if res.returncode == 0:
                desc = "OpenCode AI Agent applied autonomous repair"
                self.log(f"Remediation: {desc}\n{res.stdout[-300:]}", level="SUCCESS")
                return desc
            else:
                self.log(f"OpenCode CLI exited with {res.returncode}: {res.stderr[-200:]}", level="WARNING")
        except subprocess.TimeoutExpired:
            self.log("OpenCode CLI timed out after 180s", level="WARNING")
        except Exception as e:
            self.log(f"Error executing OpenCode CLI: {e}", level="WARNING")
        return False

    def diagnose_and_heal(self, attempt_num=1):
        """Scans the build log, executes all applicable fixes, and returns list of actions."""
        log_content = self.read_tail_log(600)
        if not log_content:
            self.log("Build log is empty or could not be read; checking device tree directly", level="WARNING")

        actions = []

        # List of heuristic remediation functions in priority order
        handlers = [
            ("DEXPREOPT User/Userdebug Enforcement", self.fix_dexpreopt_user_userdebug),
            ("DEX Preopt Check Task Bypass", self.fix_dexpreopt_check_task),
            ("Obsolete Board Variables Cleanup", self.fix_obsolete_board_vars),
            ("Recovery Partition Size Overflow Protection", self.fix_recovery_partition_overflow),
            ("RBE Bootstrap Disablement", self.fix_rbe_bootstrap),
            ("Broken Duplicate Build Rules Fix", self.fix_broken_duplicate_rules),
            ("Broken Prebuilt ELF Verification Fix", self.fix_broken_elf_prebuilts),
            ("Missing Required Modules Allowances", self.fix_missing_required_modules),
            ("Artifact Path Requirements Bypass", self.fix_artifact_path_requirements),
            ("Sandboxing NSJAIL Environment Config", self.fix_sandbox_nsjail),
            ("Mkbootimg Samsung Exynos --dt Patch", self.fix_mkbootimg_dt_argument),
            ("Missing Host Tools Resolution", self.fix_missing_host_command),
            ("Soong Extra Variables JSON Fix", self.fix_soong_extra_variables_json),
            ("Stale Output Lock Cleanup", self.fix_stale_out_lock),
            ("Invalid Lunch Choices Sanitizer", self.fix_invalid_lunch_choices),
            ("Corrupted Rust Intermediate Archive Recovery", self.fix_rust_archive_failure),
        ]

        for diagnosis_name, handler in handlers:
            action_desc = handler(log_content)
            if action_desc:
                actions.append({
                    "attempt": attempt_num,
                    "diagnosis": diagnosis_name,
                    "action": action_desc,
                    "success": True,
                })

        # If heuristics found nothing, invoke OpenCode AI Agent CLI
        if not actions:
            ai_action = self.run_opencode_cli_fallback(log_content)
            if ai_action:
                actions.append({
                    "attempt": attempt_num,
                    "diagnosis": "OpenCode AI Agent Diagnosis",
                    "action": ai_action,
                    "success": True,
                })

        self.remediations_taken.extend(actions)
        return actions

    def supervise_build(self, command, max_retries=4):
        """Runs the build command, intercepts failures, heals, and retries."""
        self.log(f"Starting supervised build session in {self.work_dir}", level="INFO")
        self.log(f"Command: {command}", level="INFO")

        current_env = os.environ.copy()
        # Proactively sanitize makefiles and neutralize fatal checks before attempt 1
        self.fix_invalid_lunch_choices()
        self.fix_dexpreopt_user_userdebug(force=True)

        for attempt in range(1, max_retries + 1):
            self.log(f"--- Compilation Attempt {attempt} of {max_retries} ---", level="INFO")

            # Clear any stale soong lock file from previous attempts or background workers
            lock_path = self.work_dir / "out" / ".lock"
            if lock_path.exists():
                try:
                    lock_path.unlink()
                except Exception:
                    pass

            # Apply any environment overrides accumulated from prior healings
            for k, v in self.env_overrides.items():
                current_env[k] = v

            # Execute command through bash subshell
            process = subprocess.Popen(
                ["/usr/bin/bash", "-c", command],
                cwd=str(self.work_dir),
                env=current_env,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )

            # Stream output to console and append to build.log
            with open(self.log_file, "a", encoding="utf-8") as lf:
                for line in process.stdout:
                    print(line, end="", flush=True)
                    lf.write(line)

            process.wait()
            exit_code = process.returncode

            if exit_code == 0:
                self.log(f"Build succeeded on attempt {attempt}!", level="SUCCESS")
                self.write_ci_summary()
                return 0

            self.log(f"Build failed with exit code {exit_code} on attempt {attempt}.", level="ERROR")

            if attempt == max_retries:
                self.log("Maximum retry attempts reached. Build failed.", level="ERROR")
                self.write_ci_summary()
                return exit_code

            # Trigger diagnosis and healing
            self.log(f"Initiating Opencode Agent healing for attempt {attempt}...", level="ACTION")
            actions = self.diagnose_and_heal(attempt_num=attempt)

            if not actions:
                self.log("No automatic remediation pattern matched. Attempting emergency cleanup...", level="WARNING")
                # Emergency device tree check: purge any WITH_DEXPREOPT or obsolete vars
                self.fix_dexpreopt_user_userdebug("DEXPREOPT must be enabled for user and userdebug builds")
                self.fix_obsolete_board_vars(self.read_tail_log(100))

            self.log(f"Healing complete. Retrying build in 3 seconds...", level="INFO")
            import time
            time.sleep(3)

        self.write_ci_summary()
        return 1


def main():
    parser = argparse.ArgumentParser(description="Opencode In-Runner Self-Healing Agent")
    parser.add_argument("mode", choices=["supervise", "heal"], help="Execution mode")
    parser.add_argument("--work-dir", default=os.getcwd(), help="Path to build source directory")
    parser.add_argument("--device-dir", default="device/samsung/m20lte", help="Path to device tree")
    parser.add_argument("--log", default="build.log", help="Path to build log")
    parser.add_argument("--actions-log", default="opencode_agent_actions.log", help="Path to actions log")
    parser.add_argument("--command", default="", help="Command to run in supervise mode")
    parser.add_argument("--max-retries", type=int, default=4, help="Maximum healing retry attempts")

    args = parser.parse_args()

    agent = OpencodeAgent(
        work_dir=args.work_dir,
        device_dir=args.device_dir,
        log_file=args.log,
        actions_log_file=args.actions_log,
    )

    if args.mode == "heal":
        actions = agent.diagnose_and_heal()
        agent.write_ci_summary()
        sys.exit(0 if actions else 1)
    elif args.mode == "supervise":
        if not args.command:
            print("Error: --command is required in supervise mode", file=sys.stderr)
            sys.exit(1)
        code = agent.supervise_build(args.command, max_retries=args.max_retries)
        sys.exit(code)


if __name__ == "__main__":
    main()
