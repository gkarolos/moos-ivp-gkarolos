#!/bin/bash -e
#----------------------------------------------------------
#  Script: launch.sh
#  Author: Michael Benjamin
#  LastEd: May 20th 2019  (A8 bonus: --dist / --depth via nsplug)
#----------------------------------------------------------
#  Part 1: Set Exit actions and declare global var defaults
#----------------------------------------------------------
TIME_WARP=1
COMMUNITY="bravo"
GUI="yes"
RETURN_DIST=200      # return home after this many meters traveled at depth
DEPTH_THRESH=25      # only count distance while NAV_DEPTH exceeds this (m)

#----------------------------------------------------------
#  Part 2: Check for and handle command-line arguments
#----------------------------------------------------------
for ARGI; do
    if [ "${ARGI}" = "--help" -o "${ARGI}" = "-h" ] ; then
	echo "launch.sh [SWITCHES] [time_warp]                          "
	echo "  --help, -h        Show this help message                "
	echo "  --nogui           Launch without pMarineViewer          "
	echo "  --dist=<meters>   Return home after this distance is     "
	echo "                    traveled at depth (default $RETURN_DIST)"
	echo "  --depth=<meters>  Count distance only when NAV_DEPTH      "
	echo "                    exceeds this (default $DEPTH_THRESH)    "
	exit 0;
    elif [ "${ARGI}" = "--nogui" ] ; then
	GUI="no"
    elif [ "${ARGI:0:7}" = "--dist=" ] ; then
        RETURN_DIST="${ARGI#--dist=}"
    elif [ "${ARGI:0:8}" = "--depth=" ] ; then
        DEPTH_THRESH="${ARGI#--depth=}"
    elif [ "${ARGI//[^0-9]/}" = "$ARGI" -a "$TIME_WARP" = 1 ]; then
        TIME_WARP=$ARGI
    else
        echo "launch.sh Bad arg:" $ARGI " Exiting with code: 1"
        exit 1
    fi
done

#----------------------------------------------------------
#  Part 3: Expand the meta files with nsplug
#----------------------------------------------------------
# nsplug substitutes $(MACRO) with the MACRO=VALUE pairs below, turning the
# meta_*.moos / meta_*.bhv templates into ready-to-run targ_*.moos / targ_*.bhv.
nsplug meta_$COMMUNITY.moos targ_$COMMUNITY.moos -f \
       RETURN_DIST=$RETURN_DIST DEPTH_THRESH=$DEPTH_THRESH
nsplug meta_$COMMUNITY.bhv  targ_$COMMUNITY.bhv  -f \
       RETURN_DIST=$RETURN_DIST DEPTH_THRESH=$DEPTH_THRESH

#----------------------------------------------------------
#  Part 4: Launch the processes
#----------------------------------------------------------
echo "Launching $COMMUNITY MOOS Community with WARP: $TIME_WARP"
echo "  return distance = $RETURN_DIST m,  depth threshold = $DEPTH_THRESH m"
pAntler targ_$COMMUNITY.moos --MOOSTimeWarp=$TIME_WARP >& /dev/null &

uMAC -t targ_$COMMUNITY.moos
kill -- -$$
