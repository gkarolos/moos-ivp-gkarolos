#!/bin/bash -e
#----------------------------------------------------------
#  Script: launch.sh   (Lab 6 - Exercise 1: Alpha pShare)
#  Launches TWO MOOS communities: shoreside + alpha vehicle.
#----------------------------------------------------------
TIME_WARP=1
GUI="yes"

#----------------------------------------------------------
#  Part 1: Handle command-line arguments
#----------------------------------------------------------
for ARGI; do
    if [ "${ARGI}" = "--help" -o "${ARGI}" = "-h" ] ; then
	echo "launch.sh [SWITCHES] [time_warp]   "
	echo "  --help, -h    Show this help message            "
	echo "  --nogui       Launch the vehicle only, no viewer "
	exit 0;
    elif [ "${ARGI}" = "--nogui" ] ; then
	GUI="no"
    elif [ "${ARGI//[^0-9]/}" = "$ARGI" -a "$TIME_WARP" = 1 ]; then
        TIME_WARP=$ARGI
    else
        echo "launch.sh Bad arg:" $ARGI " Exiting with code: 1"
        exit 1
    fi
done

#----------------------------------------------------------
#  Part 2: Launch the communities
#----------------------------------------------------------
echo "Launching the ALPHA vehicle community with WARP: $TIME_WARP"
pAntler alpha.moos --MOOSTimeWarp=$TIME_WARP >& /dev/null &

if [ "${GUI}" = "yes" ]; then
    echo "Launching the SHORESIDE community with WARP: $TIME_WARP"
    pAntler shoreside.moos --MOOSTimeWarp=$TIME_WARP >& /dev/null &
fi

#----------------------------------------------------------
#  Part 3: Launch uMAC and clean up on exit
#----------------------------------------------------------
uMAC shoreside.moos
echo "Killing all processes ..."
kill -- -$$
