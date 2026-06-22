/************************************************************/
/*    NAME: Karolos Geroulanos                              */
/*    ORGN: MIT                                             */
/*    FILE: GenRescue.cpp                                   */
/*    DATE: April 18th, 2022                                */
/************************************************************/

#include <iterator>
#include <algorithm>
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
    // 1. Drop any planned stops that are no longer pending swimmers (rescued,
    //    or otherwise gone), preserving the order of the ones that remain.
    vector<string> kept;
    for(unsigned int i=0; i<m_tour.size(); i++)
      if(m_swimmers.count(m_tour[i]) != 0)
        kept.push_back(m_tour[i]);
    m_tour = kept;

    // 2. Slot any newly-known swimmers into the existing route at their
    //    cheapest insertion point (#1). This keeps the boat's current heading
    //    and only diverts when it genuinely pays -- no disruptive full rebuild.
    map<string, XYPoint>::iterator q;
    for(q = m_swimmers.begin(); q != m_swimmers.end(); q++) {
      bool present = false;
      for(unsigned int i=0; i<m_tour.size(); i++)
        if(m_tour[i] == q->first) { present = true; break; }
      if(!present)
        cheapestInsert(q->first);
    }

    // 3. Polish the order with 2-opt + Or-opt (strictly-shorter moves only, #2).
    //    This is the NEW method's candidate route.
    optimizeTour();
    vector<string> cand_new = m_tour;

    // 3b. Best-of-both safeguard: also compute the OLD method (greedy
    //     nearest-neighbour + 2-opt) from scratch, and drive whichever full
    //     route is shorter from the current position. Keeps every gain of the
    //     new method while guaranteeing we are never worse than the old one.
    vector<string> cand_old = greedyTour();
    twoOptIds(cand_old);
    if((tourLength(cand_old) + 1e-6) < tourLength(cand_new))
      m_tour = cand_old;
    else
      m_tour = cand_new;

    // 4. Materialise the ordered ids into the coordinate path we post/draw.
    m_path = XYSegList();
    for(unsigned int i=0; i<m_tour.size(); i++)
      m_path.add_vertex(m_swimmers[m_tour[i]].x(), m_swimmers[m_tour[i]].y());

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
// Procedure: cheapestInsert()      (fine-tuning #1)
//   Purpose: Insert one swimmer id into m_tour at the position that adds the
//            least travel distance to the route. Letting a mid-mission swimmer
//            join the EXISTING order (rather than triggering a from-scratch
//            rebuild) keeps the boat's current heading and avoids U-turns.

void GenRescue::cheapestInsert(string id)
{
  double nx = m_swimmers[id].x();
  double ny = m_swimmers[id].y();

  // Empty tour: the new swimmer is simply the first (and only) stop.
  if(m_tour.empty()) {
    m_tour.push_back(id);
    return;
  }

  // Try every slot (before each existing stop, and at the very end); keep the
  // one that adds the least travel. Slot p means: ...prev -> NEW -> next...
  // where prev is ownship when p==0.
  double       best_added = -1;
  unsigned int best_pos   = 0;
  for(unsigned int p=0; p<=m_tour.size(); p++) {
    double px, py;                       // the point just before slot p
    if(p == 0) { px = m_nav_x; py = m_nav_y; }
    else       { px = m_swimmers[m_tour[p-1]].x(); py = m_swimmers[m_tour[p-1]].y(); }

    double added;
    if(p == m_tour.size()) {
      // Appending at the end only adds the leg prev -> NEW.
      added = distPointToPoint(px, py, nx, ny);
    }
    else {
      // Splicing between prev and next: prev->NEW->next minus prev->next.
      double qx = m_swimmers[m_tour[p]].x();
      double qy = m_swimmers[m_tour[p]].y();
      added = distPointToPoint(px,py,nx,ny) + distPointToPoint(nx,ny,qx,qy)
            - distPointToPoint(px,py,qx,qy);
    }

    if((best_added < 0) || (added < best_added)) {
      best_added = added;
      best_pos   = p;
    }
  }
  m_tour.insert(m_tour.begin() + best_pos, id);
}

//---------------------------------------------------------
// Procedure: tourLength()
//   Purpose: Total travel length of an ordered swimmer-id list, measured from
//            the ownship position through the stops in order (an open path --
//            no return leg, matching the rescue task).

double GenRescue::tourLength(const vector<string>& order)
{
  if(order.empty())
    return(0);

  // Leg from ownship to the first stop.
  double total = distPointToPoint(m_nav_x, m_nav_y,
                                  m_swimmers[order[0]].x(),
                                  m_swimmers[order[0]].y());
  // Legs between successive stops.
  for(unsigned int i=1; i<order.size(); i++)
    total += distPointToPoint(m_swimmers[order[i-1]].x(), m_swimmers[order[i-1]].y(),
                              m_swimmers[order[i]].x(),   m_swimmers[order[i]].y());
  return(total);
}

//---------------------------------------------------------
// Procedure: optimizeTour()        (fine-tuning #2)
//   Purpose: Shorten m_tour in place using two local-search moves:
//              - 2-opt:  reverse a sub-segment (un-crosses legs)
//              - Or-opt: lift out a short chain (1..3 stops) and reinsert it
//                        somewhere cheaper
//            Each candidate is a full reordering of the SAME ids, accepted only
//            if the whole tour gets strictly shorter -- so a swimmer can never
//            be dropped, duplicated, or the route lengthened. Sweeps to a local
//            optimum. (Swimmer counts are small, so the O(n^2)/O(n^3) rebuilds
//            here are negligible at helm rates.)

void GenRescue::optimizeTour()
{
  if(m_tour.size() < 2)
    return;

  bool improved = true;
  while(improved) {
    improved = false;
    double best = tourLength(m_tour);

    // --- 2-opt: reverse each sub-segment [i..k]; keep it if the tour shrank.
    for(unsigned int i=0; (i+1) < m_tour.size(); i++) {
      for(unsigned int k=i+1; k < m_tour.size(); k++) {
        vector<string> cand = m_tour;
        reverse(cand.begin()+i, cand.begin()+k+1);
        double len = tourLength(cand);
        if((len + 1e-6) < best) {
          m_tour = cand; best = len; improved = true;
        }
      }
    }

    // --- Or-opt: lift out a chain of L stops (1..3) and reinsert it elsewhere.
    for(unsigned int L=1; (L<=3) && (L < m_tour.size()); L++) {
      for(unsigned int i=0; (i+L) <= m_tour.size(); i++) {
        vector<string> chain(m_tour.begin()+i, m_tour.begin()+i+L);
        vector<string> rest  = m_tour;
        rest.erase(rest.begin()+i, rest.begin()+i+L);
        for(unsigned int p=0; p <= rest.size(); p++) {
          vector<string> cand = rest;
          cand.insert(cand.begin()+p, chain.begin(), chain.end());
          double len = tourLength(cand);
          if((len + 1e-6) < best) {
            m_tour = cand; best = len; improved = true;
          }
        }
      }
    }
  }
}

//---------------------------------------------------------
// Procedure: greedyTour()          (old planning method, part 1)
//   Purpose: Order all current swimmers by nearest-neighbour from ownship.
//            Used by the best-of-both safeguard in postShortestPath().

vector<string> GenRescue::greedyTour()
{
  vector<string> remaining;
  for(map<string,XYPoint>::iterator q=m_swimmers.begin(); q!=m_swimmers.end(); q++)
    remaining.push_back(q->first);

  vector<string> order;
  double cx = m_nav_x, cy = m_nav_y;
  while(!remaining.empty()) {
    unsigned int best_i = 0;
    double       best_d = -1;
    for(unsigned int i=0; i<remaining.size(); i++) {
      double d = distPointToPoint(cx, cy, m_swimmers[remaining[i]].x(),
                                          m_swimmers[remaining[i]].y());
      if((best_d < 0) || (d < best_d)) { best_d = d; best_i = i; }
    }
    string id = remaining[best_i];
    order.push_back(id);
    cx = m_swimmers[id].x();
    cy = m_swimmers[id].y();
    remaining.erase(remaining.begin() + best_i);
  }
  return(order);
}

//---------------------------------------------------------
// Procedure: twoOptIds()           (old planning method, part 2)
//   Purpose: Shorten an ordered id list with 2-opt (un-cross legs) only.
//            Same strictly-shorter-only rule, so it never drops a swimmer.

void GenRescue::twoOptIds(vector<string>& order)
{
  if(order.size() < 3)
    return;

  bool improved = true;
  while(improved) {
    improved = false;
    double best = tourLength(order);
    for(unsigned int i=0; (i+1) < order.size(); i++) {
      for(unsigned int k=i+1; k < order.size(); k++) {
        vector<string> cand = order;
        reverse(cand.begin()+i, cand.begin()+k+1);
        double len = tourLength(cand);
        if((len + 1e-6) < best) {
          order = cand; best = len; improved = true;
        }
      }
    }
  }
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
