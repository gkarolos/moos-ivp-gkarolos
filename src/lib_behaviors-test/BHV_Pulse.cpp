/************************************************************/
/*    NAME: Karolos Geroulanos                              */
/*    ORGN: MIT                                             */
/*    FILE: BHV_Pulse.cpp                                   */
/*    DATE: 2026-06-23                                      */
/************************************************************/

#include <iterator>
#include <cstdlib>
#include "MBUtils.h"
#include "BuildUtils.h"
#include "BHV_Pulse.h"

using namespace std;

// Seconds to wait after a waypoint is hit before firing the pulse.
static const double PULSE_DELAY = 5.0;

//---------------------------------------------------------------
// Constructor

BHV_Pulse::BHV_Pulse(IvPDomain domain) :
  IvPBehavior(domain)
{
  // Provide a default behavior name
  IvPBehavior::setParam("name", "defaultname");

  // This behavior only posts a visual artifact; it does not influence
  // the helm decision, so it needs no decision domain.

  // Default values for configuration parameters
  m_pulse_range    = 20;
  m_pulse_duration = 4;

  // Default values for behavior state variables
  m_osx           = 0;
  m_osy           = 0;
  m_prev_index    = 0;
  m_index_set     = false;
  m_pulse_pending = false;
  m_wpt_hit_utc   = 0;

  // Subscribe for ownship position and the sister waypoint behavior's
  // current waypoint index.
  addInfoVars("NAV_X, NAV_Y, WPT_INDEX");
}

//---------------------------------------------------------------
// Procedure: setParam()

bool BHV_Pulse::setParam(string param, string val)
{
  // Let the base class handle standard params (name, pwt, condition, ...)
  if(IvPBehavior::setParam(param, val))
    return(true);

  param = tolower(param);
  double double_val = atof(val.c_str());

  if((param == "pulse_range") && isNumber(val) && (double_val > 0)) {
    m_pulse_range = double_val;
    return(true);
  }
  else if((param == "pulse_duration") && isNumber(val) && (double_val > 0)) {
    m_pulse_duration = double_val;
    return(true);
  }

  // If not handled above, then just return false;
  return(false);
}

//---------------------------------------------------------------
// Procedure: onSetParamComplete()
//   Purpose: Invoked once after all parameters have been handled.

void BHV_Pulse::onSetParamComplete()
{
  if(m_pulse_range <= 0)
    postEMessage("pulse_range must be greater than zero");
  if(m_pulse_duration <= 0)
    postEMessage("pulse_duration must be greater than zero");
}

//---------------------------------------------------------------
// Procedure: onHelmStart()

void BHV_Pulse::onHelmStart()
{
}

//---------------------------------------------------------------
// Procedure: onIdleState()

void BHV_Pulse::onIdleState()
{
}

//---------------------------------------------------------------
// Procedure: onCompleteState()

void BHV_Pulse::onCompleteState()
{
}

//---------------------------------------------------------------
// Procedure: postConfigStatus()

void BHV_Pulse::postConfigStatus()
{
}

//---------------------------------------------------------------
// Procedure: onIdleToRunState()

void BHV_Pulse::onIdleToRunState()
{
}

//---------------------------------------------------------------
// Procedure: onRunToIdleState()

void BHV_Pulse::onRunToIdleState()
{
}

//---------------------------------------------------------------
// Procedure: onRunState()
//   Purpose: Each iteration, watch the waypoint index. When it changes
//            (a waypoint was just reached), arm a pulse and fire it
//            PULSE_DELAY seconds later at ownship's position.

IvPFunction* BHV_Pulse::onRunState()
{
  // Part 1: Read ownship position and the current buffer time.
  bool ok_x, ok_y;
  m_osx = getBufferDoubleVal("NAV_X", ok_x);
  m_osy = getBufferDoubleVal("NAV_Y", ok_y);
  if(!ok_x || !ok_y) {
    postWMessage("No ownship NAV_X/NAV_Y in info buffer.");
    return(0);
  }
  double curr_time = getBufferCurrTime();

  // Part 2: Read the waypoint index posted by the sister waypoint
  // behavior. If it is not present yet, there is nothing to do.
  bool ok_i;
  double curr_index = getBufferDoubleVal("WPT_INDEX", ok_i);
  if(ok_i) {
    if(!m_index_set) {
      // First time we see an index: remember it, but don't fire.
      m_prev_index = curr_index;
      m_index_set  = true;
    }
    else if(curr_index != m_prev_index) {
      // The waypoint index changed -> a waypoint was just reached.
      m_prev_index    = curr_index;
      m_wpt_hit_utc   = curr_time;
      m_pulse_pending = true;
    }
  }

  // Part 3: If a pulse is armed and PULSE_DELAY has elapsed, fire it.
  //
  // We build the VIEW_RANGE_PULSE spec string directly instead of using
  // XYRangePulse::set_color(). set_color() references ColorPack, a symbol the
  // pHelmIvP binary does not export to dynamically-loaded behaviors (the helm
  // never renders pulses itself), so calling it aborts the helm the instant
  // the pulse fires. This hand-built spec uses only postMessage and
  // doubleToStringX, and is exactly the format pMarineViewer parses.
  if(m_pulse_pending && ((curr_time - m_wpt_hit_utc) >= PULSE_DELAY)) {
    string spec;
    spec  = "x="         + doubleToStringX(m_osx, 2);
    spec += ",y="        + doubleToStringX(m_osy, 2);
    spec += ",radius="   + doubleToStringX(m_pulse_range, 1);
    spec += ",duration=" + doubleToStringX(m_pulse_duration, 1);
    spec += ",fill=0.25,label=bhv_pulse";
    spec += ",edge_color=yellow,fill_color=yellow,edge_size=1";
    spec += ",time="     + doubleToStringX(curr_time, 2);
    postMessage("VIEW_RANGE_PULSE", spec);
    m_pulse_pending = false;
  }

  // Posting-only behavior: never returns an objective function.
  return(0);
}
