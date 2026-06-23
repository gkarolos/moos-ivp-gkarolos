/************************************************************/
/*    NAME: Karolos Geroulanos                              */
/*    ORGN: MIT                                             */
/*    FILE: GenRescue.cpp                                   */
/*    DATE: April 18th, 2022                                */
/************************************************************/

#include <iterator>
#include <algorithm>
#include <cmath>
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

  m_rival_x   = 0;
  m_rival_y   = 0;
  m_rival_hdg = 0;
  m_rival_spd = 0;
  m_rival_utc = 0;
  m_rival_set = false;
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
    else if(key == "NODE_REPORT")
      handled = handleMailNodeReport(sval);
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

  // Refresh rival-awareness first: this may concede/reclaim swimmers and set
  // m_path_pending so the planner below reacts to the rival's movement.
  updateWeights();

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

  // The rescue missions don't set a vname param, so fall back to the MOOS
  // community name (which IS the vehicle name) -- needed so we can tell our
  // OWN NODE_REPORT from the rival's in handleMailNodeReport().
  if(m_vname == "")
    m_MissionReader.GetValue("Community", m_vname);

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
  // Competitor-awareness: the rival's position reports (NAME != our vname).
  Register("NODE_REPORT", 0);
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
// Procedure: handleMailNodeReport()
//   Parse a NODE_REPORT. Anything whose NAME isn't our own vname is "the
//   rival" -- robust to whoever we're drawn against (alex, abe, ...).

bool GenRescue::handleMailNodeReport(string str)
{
  string name;
  if(!tokParse(str, "NAME", ',', '=', name))
    return(false);
  if(name == m_vname)        // that's our own report; ignore
    return(true);

  double x, y, hdg = 0, spd = 0;
  if(!tokParse(str, "X", ',', '=', x)) return(false);
  if(!tokParse(str, "Y", ',', '=', y)) return(false);
  tokParse(str, "HDG", ',', '=', hdg);   // optional
  tokParse(str, "SPD", ',', '=', spd);   // optional

  m_rival_x = x;  m_rival_y = y;
  m_rival_hdg = hdg;  m_rival_spd = spd;
  m_rival_utc = MOOSTime();
  m_rival_set = true;
  return(true);
}

//---------------------------------------------------------
// Procedure: rivalWeight()
//   Returns w in [0,1]: how much this swimmer is OURS to pursue.
//   1.0 = fully ours; ->0 = the rival clearly wins the race for it.
//   Falls back to 1.0 whenever we have no fresh rival data, so out of
//   comms range the planner behaves exactly as it does today.

double GenRescue::rivalWeight(string id)
{
  const double STALE_SECS = 3.0;   // older rival data -> treat as no data
                                   // (tightened 5->3 so decisions stay current)
  const double PHI_MAX    = 45.0;  // rival heading gate: must be clearly pointed at it
  const double TAU        = 8.0;   // race "fuzziness" (meters; tune later)
  const double DEG        = 180.0 / 3.14159265358979;

  if(!m_rival_set)                            return(1.0);
  if(!m_nav_x_set || !m_nav_y_set)            return(1.0);
  if((MOOSTime() - m_rival_utc) > STALE_SECS) return(1.0);

  double sx = m_swimmers[id].x();
  double sy = m_swimmers[id].y();

  double d_own   = distPointToPoint(m_nav_x,   m_nav_y,   sx, sy);
  double d_rival = distPointToPoint(m_rival_x, m_rival_y, sx, sy);

  // Heading gate: compass bearing rival->swimmer vs the rival's heading.
  double brg = atan2(sx - m_rival_x, sy - m_rival_y) * DEG;  // 0=North, CW
  if(brg < 0) brg += 360.0;
  double dphi = fabs(brg - m_rival_hdg);
  if(dphi > 180.0) dphi = 360.0 - dphi;
  if(dphi >= PHI_MAX)        // rival driving away -> fully ours
    return(1.0);

  // Equal-speed assumption: ETA proportional to distance. Race margin
  // delta>0 => we arrive first. Logistic squashes it into [0,1].
  double delta = d_rival - d_own;
  return(1.0 / (1.0 + exp(-delta / TAU)));
}

//---------------------------------------------------------
// Procedure: updateWeights()
//   Refresh every swimmer's smoothed weight and flip the conceded set with
//   hysteresis around 0.5 (rival closer -> concede, we're closer -> reclaim).
//   If the conceded set changes, request a replan. Called once per Iterate().

void GenRescue::updateWeights()
{
  // Strategy: concede a swimmer whenever the rival will reach it first (rival
  // closer), and reclaim it once we are clearly the closer boat again. The
  // weight w is ~0.5 at equal distance, >0.5 when WE are closer, <0.5 when the
  // rival is closer (see rivalWeight). The dead-band around 0.5 plus smoothing
  // prevents flip-flopping on near-ties / noisy reports.
  //
  // NOTE: there is deliberately NO "never abandon my current target" rule. If
  // the rival is closer to the swimmer we are driving to, we DROP it and head
  // to the nearest remaining one instead (Karolos's competition strategy).
  const double CONCEDE_LO = 0.45;  // rival clearly closer -> concede
  const double RECLAIM_HI = 0.55;  // we are clearly closer  -> reclaim

  bool changed = false;

  map<string, XYPoint>::iterator q;
  for(q = m_swimmers.begin(); q != m_swimmers.end(); q++) {
    string id  = q->first;
    double raw = rivalWeight(id);

    // Exponential smoothing so one noisy report can't yank the plan around
    // (0.45 latest / 0.55 previous -> reacts quickly but still filtered).
    double prev = (m_weight.count(id) ? m_weight[id] : 1.0);
    double w    = 0.45*raw + 0.55*prev;
    m_weight[id] = w;

    bool conceded = (m_conceded.count(id) != 0);
    if(!conceded && (w < CONCEDE_LO))     { m_conceded.insert(id); changed = true; }
    else if(conceded && (w > RECLAIM_HI)) { m_conceded.erase(id);  changed = true; }
  }

  // Forget swimmers that no longer exist (rescued / gone).
  vector<string> dead;
  for(set<string>::iterator c=m_conceded.begin(); c!=m_conceded.end(); c++)
    if(m_swimmers.count(*c) == 0) dead.push_back(*c);
  for(unsigned int i=0; i<dead.size(); i++) {
    m_conceded.erase(dead[i]);
    m_weight.erase(dead[i]);
  }

  if(changed)
    m_path_pending = true;
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
    // 0. Competitor-awareness filter (Option A): plan only the swimmers we are
    //    NOT conceding to the rival. ALL-CONCEDED FALLBACK: if every known
    //    swimmer is conceded, plan over all of them this tick rather than
    //    freezing (worst case we just trail the rival, like the old code).
    set<string> conceded_now = m_conceded;
    {
      unsigned int eligible = 0;
      for(map<string,XYPoint>::iterator c=m_swimmers.begin(); c!=m_swimmers.end(); c++)
        if(conceded_now.count(c->first) == 0) eligible++;
      if(eligible == 0)
        conceded_now.clear();
    }

    // 1. Plan: ALWAYS head to the nearest swimmer first, then nearest-from-there
    //    (greedy nearest-neighbour from the boat's current position), skipping
    //    any swimmer we are conceding to the rival. Recomputed only on a swimmer
    //    or concession change (m_path_pending), so it doesn't thrash while the
    //    boat is mid-leg between two stops.
    m_tour = greedyTour(conceded_now);

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

vector<string> GenRescue::greedyTour(const set<string>& skip)
{
  vector<string> remaining;
  for(map<string,XYPoint>::iterator q=m_swimmers.begin(); q!=m_swimmers.end(); q++)
    if(skip.count(q->first) == 0)
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
  m_msgs << "Rival seen:        " << boolToString(m_rival_set) << endl;
  if(m_rival_set) {
    double rival_age = MOOSTime() - m_rival_utc;
    m_msgs << "Rival (x,y,hdg):   (" << doubleToStringX(m_rival_x,1) << ", "
           << doubleToStringX(m_rival_y,1) << ", " << doubleToStringX(m_rival_hdg,0)
           << ")  age=" << doubleToStringX(rival_age,1) << "s" << endl;
  }
  m_msgs << "Swimmers conceded: " << m_conceded.size() << endl;
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
    double w    = (m_weight.count(q->first) ? m_weight[q->first] : 1.0);
    string mark = (m_conceded.count(q->first) ? "  <-- CONCEDED" : "");
    m_msgs << "  " << q->first << ": ("
           << doubleToStringX(pt.x(),1) << ", "
           << doubleToStringX(pt.y(),1) << ")  w=" << doubleToStringX(w,2)
           << mark << endl;
  }
  return(true);
}
