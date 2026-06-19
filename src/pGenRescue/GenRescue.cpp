/************************************************************/
/*    NAME: Mike Benjamin                                   */
/*    ORGN: MIT                                             */
/*    FILE: GenRescue.cpp                                   */
/*    DATE: April 18th, 2022                                */
/************************************************************/

#include <iterator>
#include "GenRescue.h"
#include "MBUtils.h"
#include "ColorParse.h"
#include "XYPoint.h"
#include "XYSegList.h"
#include "GeomUtils.h"
#include "PathUtils.h"
#include "XYFormatUtilsPoly.h"
#include "XYFieldGenerator.h"

using namespace std;

//---------------------------------------------------------
// Constructor()

GenRescue::GenRescue()
{
  // Initialize state variables
  m_nav_x = 0;
  m_nav_y = 0;
  m_nav_x_set = 0;
  m_nav_y_set = 0;

  m_path_pending  = false;
  m_total_rescued = 0;
}

//---------------------------------------------------------
// Procedure: OnNewMail()

bool GenRescue::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  MOOSMSG_LIST::iterator p;
  for(p=NewMail.begin(); p!=NewMail.end(); p++) {
    CMOOSMsg &msg = *p;
    string key  = msg.GetKey();
    string sval = msg.GetString();

    bool handled = true;
    if(key == "SWIMMER_ALERT") 
      handled = handleMailNewSwimmer(sval);
    else if(key == "FOUND_SWIMMER") 
      handled = handleMailFoundSwimmer(sval);
    else if(key == "NAV_X") {
      m_nav_x = msg.GetDouble();
      m_nav_x_set = true;
    }
    else if(key == "NAV_Y") {
      m_nav_y = msg.GetDouble();
      m_nav_y_set = true;
    }

    else if(key != "APPCAST_REQ") // handle by AppCastingMOOSApp
      handled = false;
    
    if(!handled)
      reportRunWarning("Unhandled Mail: " + key +"=" + sval);
    
  }
  return(true);
}
 
//---------------------------------------------------------
// Procedure: OnConnectToServer()

bool GenRescue::OnConnectToServer()
{
  RegisterVariables();
  return(true);
}

//---------------------------------------------------------
// Procedure: Iterate()

bool GenRescue::Iterate()
{
  AppCastingMOOSApp::Iterate();

  // Replan immediately whenever a swimmer was just added or removed
  // (m_path_pending), and also re-post periodically so the helm still
  // receives our path even if it wasn't listening yet (e.g. before DEPLOY).
  if(m_path_pending || ((m_iteration % 20) == 0))
    postShortestPath();

  AppCastingMOOSApp::PostReport();
  return(true);
}

//---------------------------------------------------------
// Procedure: OnStartUp()

bool GenRescue::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp(); 

  STRING_LIST sParams;
  m_MissionReader.GetConfiguration(GetAppName(), sParams);
  
  STRING_LIST::iterator p;
  for(p=sParams.begin(); p!=sParams.end(); p++) {
    string sLine  = *p;
    string param  = tolower(biteStringX(sLine, '='));
    string value  = sLine;
    if(param == "vname")
      m_vname = value;
  }
  
  RegisterVariables();	
  return(true);
}

//---------------------------------------------------------
// Procedure: RegisterVariables()

void GenRescue::RegisterVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  Register("SWIMMER_ALERT", 0);
  Register("FOUND_SWIMMER", 0);
  // We need our own navigation position to plan a greedy path from where
  // the vehicle actually is. The baseline handled these in OnNewMail but
  // never subscribed to them, so they never arrived.
  Register("NAV_X", 0);
  Register("NAV_Y", 0);
}


//---------------------------------------------------------
// Procedure: handleMailNewSwimmer()

bool GenRescue::handleMailNewSwimmer(string str)
{
  // Expected format, e.g.:  "x=23, y=54, id=04"
  double swim_x, swim_y;
  string swim_id;
  bool ok_x  = tokParse(str, "x",  ',', '=', swim_x);
  bool ok_y  = tokParse(str, "y",  ',', '=', swim_y);
  bool ok_id = tokParse(str, "id", ',', '=', swim_id);

  // If any field is missing, report it back as unhandled (run warning).
  if(!ok_x || !ok_y || !ok_id)
    return(false);

  // Already rescued? Ignore. Alerts keep repeating even after a rescue,
  // so without this check we would wrongly re-add the swimmer and sail
  // back to it.
  if(m_rescued.count(swim_id) != 0)
    return(true);

  // Swimmer alerts repeat every ~15s. If we already know this id, there
  // is nothing new to do -- just acknowledge it as handled.
  if(m_swimmers.count(swim_id) != 0)
    return(true);

  // New swimmer: remember its location (labeled by id) and flag a replan
  // so the next Iterate() rebuilds the path to include it.
  XYPoint pt(swim_x, swim_y);
  pt.set_label(swim_id);
  m_swimmers[swim_id] = pt;
  m_path_pending = true;

  return(true);
}

//---------------------------------------------------------
// Procedure: handleMailFoundSwimmer()

bool GenRescue::handleMailFoundSwimmer(string str)
{
  // Expected format, e.g.:  "id=01, finder=abe"
  string swim_id;
  bool ok_id = tokParse(str, "id", ',', '=', swim_id);
  if(!ok_id)
    return(false);

  // Permanently remember this swimmer as rescued, so a later (repeating)
  // SWIMMER_ALERT for it is never re-added to our target list. FOUND_SWIMMER
  // messages can repeat too, so only count a rescue the first time.
  bool newly_rescued = (m_rescued.count(swim_id) == 0);
  m_rescued.insert(swim_id);
  if(newly_rescued)
    m_total_rescued++;

  // If a swimmer we were targeting has been rescued (by us, or in later
  // labs by another vehicle), drop it from our list and flag a replan so
  // we don't keep sailing toward an already-rescued swimmer.
  if(m_swimmers.count(swim_id) != 0) {
    m_swimmers.erase(swim_id);
    m_path_pending = true;
  }
  return(true);
}

//---------------------------------------------------------
// Procedure: postShortestPath()

void GenRescue::postShortestPath()
{
  // We need our own position before we can plan a sensible greedy path.
  if(!m_nav_x_set || !m_nav_y_set)
    return;

  // With no known (unrescued) swimmers there is nothing to plan toward.
  if(m_swimmers.size() == 0)
    return;

  // Only (re)build the ordered path when the swimmer set has actually
  // changed. Rebuilding every iteration would re-order the points as the
  // vehicle moves and make it indecisive. Between changes we just re-post
  // the cached path (cheap, and keeps the helm fed -- see Iterate()).
  if(m_path_pending) {
    XYSegList segl;
    map<string, XYPoint>::iterator q;
    for(q = m_swimmers.begin(); q != m_swimmers.end(); q++)
      segl.add_vertex(q->second.x(), q->second.y());

    // Greedy nearest-neighbour ordering, starting from ownship position.
    m_path = greedyPath(segl, m_nav_x, m_nav_y);

    // Seglist needs a stable name, so the drawing updates in place.
    m_path.set_label("gen_rescue");
    m_path_pending = false;
  }

  // Draw the planned path in pMarineViewer.
  Notify("VIEW_SEGLIST", m_path.get_spec());

  // Hand the path to the survey waypoint behavior (updates = SURVEY_UPDATE).
  string update_var = "SURVEY_UPDATE";
  string update_str = "points = " + m_path.get_spec_pts();
  Notify(update_var, update_str);
  reportEvent("SURVEY_UPDATE=" + update_str);
}

//---------------------------------------------------------
// Procedure: postNullPath()
//   Purpose: If a found swimmer represents the last swimmer
//            to be found, then post a survey update essentially
// 

void GenRescue::postNullPath()
{
#if 0
  if(!m_nav_x_set || !m_nav_y_set)
    return;
  if(m_map_pts.size() != 0)
    return;
  
  XYSegList segl;
  segl.add_vertex(m_nav_x, m_nav_y);
  
  // Seglist needs a name, refer when drawging and erasing
  segl.set_label("one");
  Notify("VIEW_SEGLIST", segl.get_spec());

  string update_var = "SURVEY_UPDATE";
  string update_str = "points = " + segl.get_spec_pts();

  Notify(update_var, update_str);
  reportEvent("SURVEY_UPDATE=" + update_str);
#endif
}


//---------------------------------------------------------
// Procedure: buildReport()

bool GenRescue::buildReport()
{
  m_msgs << "Vehicle Name:      " << m_vname << endl;
  m_msgs << "Ownship NAV ready: " << boolToString(m_nav_x_set && m_nav_y_set) << endl;
  m_msgs << "Ownship (x,y):     (" << doubleToStringX(m_nav_x,1)
         << ", " << doubleToStringX(m_nav_y,1) << ")" << endl;
  m_msgs << "----------------------------------------" << endl;
  m_msgs << "Swimmers to rescue: " << m_swimmers.size() << endl;
  m_msgs << "Swimmers rescued:   " << m_total_rescued << endl;
  m_msgs << "Path waypoints:     " << m_path.size() << endl;
  m_msgs << "Replan pending:     " << boolToString(m_path_pending) << endl;
  m_msgs << "----------------------------------------" << endl;
  m_msgs << "Known swimmers (id: x, y):" << endl;

  map<string, XYPoint>::iterator q;
  for(q = m_swimmers.begin(); q != m_swimmers.end(); q++) {
    XYPoint pt = q->second;
    m_msgs << "  " << q->first << ": ("
           << doubleToStringX(pt.x(),1) << ", "
           << doubleToStringX(pt.y(),1) << ")" << endl;
  }
  return(true);
}
