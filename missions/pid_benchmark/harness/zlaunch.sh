#!/bin/bash
#------------------------------------------------------------
#   Script: zlaunch.sh  (PID benchmark HARNESS runner)
#  Purpose: Sweep YAW_PID_KP across cases by running the
#           self-evaluating stem mission once per gain, each in
#           an isolated temp copy with its own port block, then
#           aggregate the per-case results into one table.
#
#   Stem mission: ../  (missions/pid_benchmark)
#   Stem owns the grade (pMissionEval -> results.txt). This
#   harness only owns case selection, port isolation, temp
#   copies, aggregation and cleanup.
#------------------------------------------------------------
set -u

#------------------------------------------------------------
#  Part 1: Locate ourselves, the stem mission and the repo
#------------------------------------------------------------
ME=`basename "$0"`
HARNESS_DIR="$(cd "$(dirname "$0")" && pwd)"
MISSION_DIR="$(cd "$HARNESS_DIR/.." && pwd)"
REPO_DIR="$(cd "$HARNESS_DIR/../../.." && pwd)"
TEARDOWN_HELPER="$REPO_DIR/scripts/moos_scoped_teardown.sh"
RESULTS_FILE="$HARNESS_DIR/results.txt"
RUN_ROOT="$HARNESS_DIR/.runs"

#------------------------------------------------------------
#  Part 2: Defaults
#------------------------------------------------------------
TIME_WARP=10
MAX_TIME=300
CASE=""
PORT_BASE=9000
PORT_STRIDE=30
PSHARE_OFFSET=$((PORT_STRIDE / 2))
KEEP_WORKDIRS="no"
NOGUI="--nogui"

# The case matrix: one token per swept YAW_PID_KP value.
CASES="kp03 kp06 kp09 kp12 kp15"

#------------------------------------------------------------
#  Part 3: Source the scoped teardown helper
#------------------------------------------------------------
if [ -f "$TEARDOWN_HELPER" ]; then
  . "$TEARDOWN_HELPER"
else
  echo "$ME: Missing teardown helper: $TEARDOWN_HELPER"
  exit 1
fi

stop_mission_apps() {
  moos_scoped_teardown_stop_root "$1" >/dev/null 2>&1 || true
}

cleanup() {
  if [ -d "$RUN_ROOT" ]; then
    stop_mission_apps "$RUN_ROOT"
    if [ "$KEEP_WORKDIRS" != "yes" ]; then
      rm -rf "$RUN_ROOT"
    fi
  fi
}
trap cleanup EXIT

#------------------------------------------------------------
#  Part 4: Argument parsing
#------------------------------------------------------------
for ARGI; do
  if [ "${ARGI}" = "--help" -o "${ARGI}" = "-h" ]; then
    echo "$ME [OPTIONS] [time_warp]                            "
    echo "  --help, -h            Show this help message       "
    echo "  --case=<token>        Run only one case (kp03..kp15)"
    echo "  --port_base=<9000>    First MOOSDB port block       "
    echo "  --max_time=<secs>     Per-case run-time ceiling     "
    echo "  --keep_workdirs       Preserve .runs/ temp copies   "
    echo "  --gui                 Run cases with pMarineViewer  "
    echo "  --nogui               Headless (default)            "
    echo "                                                      "
    echo "  Cases: $CASES"
    exit 0
  elif [ "${ARGI//[^0-9]/}" = "$ARGI" -a "$TIME_WARP" = 10 ]; then
    TIME_WARP=$ARGI
  elif [ "${ARGI:0:7}" = "--case=" ]; then
    CASE="${ARGI#--case=*}"
  elif [ "${ARGI:0:12}" = "--port_base=" ]; then
    PORT_BASE="${ARGI#--port_base=*}"
  elif [ "${ARGI:0:11}" = "--max_time=" ]; then
    MAX_TIME="${ARGI#--max_time=*}"
  elif [ "${ARGI}" = "--keep_workdirs" ]; then
    KEEP_WORKDIRS="yes"
  elif [ "${ARGI}" = "--gui" ]; then
    NOGUI=""
  elif [ "${ARGI}" = "--nogui" -o "${ARGI}" = "-ng" ]; then
    NOGUI="--nogui"
  else
    echo "$ME: Bad Arg:[$ARGI]. Exit Code 1."
    exit 1
  fi
done

#------------------------------------------------------------
#  Part 5: Map a case token to its swept YAW_PID_KP value
#------------------------------------------------------------
get_case_config() {
  YAW_KP=""
  case "$1" in
    kp03) YAW_KP="0.3" ;;
    kp06) YAW_KP="0.6" ;;
    kp09) YAW_KP="0.9" ;;
    kp12) YAW_KP="1.2" ;;
    kp15) YAW_KP="1.5" ;;
    *)
      echo "$ME: Unknown case: $1"
      return 1
      ;;
  esac
}

#------------------------------------------------------------
#  Part 6: Copy the stem mission into a per-case temp workdir
#------------------------------------------------------------
prepare_workdir() {
  local workdir="$1"
  rm -rf "$workdir"
  mkdir -p "$workdir"
  # Copy only the runnable stem inputs (no targ_/logs/results).
  cp -p "$MISSION_DIR"/meta_*.moos "$workdir"/ 2>/dev/null
  cp -p "$MISSION_DIR"/meta_*.bhv  "$workdir"/ 2>/dev/null
  cp -p "$MISSION_DIR"/plug_*.moos "$workdir"/ 2>/dev/null
  cp -p "$MISSION_DIR"/*.sh        "$workdir"/ 2>/dev/null
  [ -f "$workdir/zlaunch.sh" ] || return 1
  return 0
}

#------------------------------------------------------------
#  Part 7: Pull the grade= token out of a result line
#------------------------------------------------------------
grade_from_line() {
  awk '{ for(i=1;i<=NF;i++) if($i ~ /^grade=/){ sub(/^grade=/,"",$i); print $i; exit } }' <<< "$1"
}

#------------------------------------------------------------
#  Part 8: Run one case in its own workdir + port block
#------------------------------------------------------------
run_case() {
  local case_name="$1"
  local case_idx="$2"
  local workdir="$RUN_ROOT/$case_name"

  if ! get_case_config "$case_name"; then
    echo "case=$case_name grade=fail reason=unknown_case"
    return 1
  fi
  if ! prepare_workdir "$workdir"; then
    echo "case=$case_name grade=fail reason=prepare_error"
    return 1
  fi

  local case_base=$((PORT_BASE + case_idx * PORT_STRIDE))
  local shore_mport=$((case_base + 0))
  local veh_mport=$((case_base + 1))
  local shore_pshare=$((case_base + PSHARE_OFFSET))
  local veh_pshare=$((case_base + PSHARE_OFFSET + 1))

  # Redirect the stem mission's (noisy) console output to a per-case log so
  # only the final normalized row reaches this function's stdout.
  local launch_rc=0
  (
    cd "$workdir" || exit 1
    : > results.txt
    ./zlaunch.sh --max_time="$MAX_TIME" $NOGUI \
      --mmod="$case_name" --yaw_kp="$YAW_KP" \
      --shore_mport="$shore_mport" --veh_mport="$veh_mport" \
      --shore_pshare="$shore_pshare" --veh_pshare="$veh_pshare" \
      "$TIME_WARP"
  ) > "$workdir/run.log" 2>&1 || launch_rc=$?

  stop_mission_apps "$workdir"

  # Build the normalized harness row (mission owns grade= and metrics).
  local row
  if [ "$launch_rc" -ne 0 ]; then
    row="case=$case_name yaw_kp=$YAW_KP grade=fail reason=launch_error launch_rc=$launch_rc"
  elif [ ! -f "$workdir/results.txt" ]; then
    row="case=$case_name yaw_kp=$YAW_KP grade=fail reason=missing_result_file"
  else
    local line
    line="$(awk 'NF {last=$0} END {print last}' "$workdir/results.txt")"
    if [ "$(grade_from_line "$line")" = "" ]; then
      row="case=$case_name yaw_kp=$YAW_KP grade=fail reason=missing_result"
    else
      row="case=$case_name yaw_kp=$YAW_KP $line"
    fi
  fi
  echo "$row"
}

#------------------------------------------------------------
#  Part 9: Decide which cases to run
#------------------------------------------------------------
RUN_CASES="$CASES"
if [ "$CASE" != "" ]; then
  RUN_CASES="$CASE"
fi

mkdir -p "$RUN_ROOT"
: > "$RESULTS_FILE"

echo "$ME: Sweeping YAW_PID_KP. cases=[$RUN_CASES] warp=$TIME_WARP port_base=$PORT_BASE"

#------------------------------------------------------------
#  Part 10: Serial execution over the selected cases
#------------------------------------------------------------
idx=0
rows=0
fails=0
for case_name in $RUN_CASES; do
  echo "------------------------------------------------------------"
  echo "$ME: Running case [$case_name]  (port block $((PORT_BASE + idx * PORT_STRIDE)))"
  row="$(run_case "$case_name" "$idx")"
  echo "$row" >> "$RESULTS_FILE"
  echo "  -> $row"
  rows=$((rows + 1))
  [ "$(grade_from_line "$row")" = "pass" ] || fails=$((fails + 1))
  stop_mission_apps "$RUN_ROOT"
  idx=$((idx + 1))
done

#------------------------------------------------------------
#  Part 11: Aggregate into a human comparison table
#------------------------------------------------------------
echo
echo "============================================================"
echo "  PID BENCHMARK  -  YAW_PID_KP sweep  (fixed survey path)"
echo "  lower mission_time / total_dist = better path tracking"
echo "============================================================"
awk '
  {
    grade=""; ykp=""; mt=""; td="";
    for(i=1;i<=NF;i++){
      if($i ~ /^yaw_kp=/){ sub(/^yaw_kp=/,"",$i); ykp=$i }
      else if($i ~ /^grade=/){ sub(/^grade=/,"",$i); grade=$i }
      else if($i ~ /^mission_time=/){ sub(/^mission_time=/,"",$i); mt=$i }
      else if($i ~ /^total_dist=/){ sub(/^total_dist=/,"",$i); td=$i }
    }
    if(mt=="") mt="-"; if(td=="") td="-";
    rows[NR]=sprintf("  %-8s  %-6s  %-10s  %-12s", ykp, grade, mt, td)
  }
  END{
    printf "  %-8s  %-6s  %-10s  %-12s\n", "YAW_KP", "GRADE", "TIME(s)", "DIST(m)"
    printf "  %-8s  %-6s  %-10s  %-12s\n", "------", "-----", "-------", "-------"
    for(r=1;r<=NR;r++) print rows[r]
  }
' "$RESULTS_FILE"
echo "============================================================"
echo "$ME: Full rows in: $RESULTS_FILE"

#------------------------------------------------------------
#  Part 12: Exit status
#------------------------------------------------------------
if [ "$rows" -eq 0 ]; then
  echo "$ME: No case rows produced - harness failure."
  exit 1
fi
if [ "$fails" -ne 0 ]; then
  echo "$ME: $fails of $rows case(s) did not grade=pass."
  exit 1
fi
echo "$ME: All $rows case(s) graded pass."
exit 0
