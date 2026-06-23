/************************************************************/
/*    NAME: Karolos Geroulanos                              */
/*    ORGN: MIT                                             */
/*    FILE: GenRescue.h                                     */
/*    DATE: April 18th, 2022                                */
/************************************************************/

#ifndef P_GEN_RESCUE_HEADER
#define P_GEN_RESCUE_HEADER

#include <vector>
#include <string>
#include <map>
#include <set>
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include "XYPoint.h"
#include "XYPolygon.h"
#include "XYSegList.h"

class GenRescue : public AppCastingMOOSApp
{
 public:
   GenRescue();
   ~GenRescue() {};

 protected:
  bool OnNewMail(MOOSMSG_LIST &NewMail);
  bool Iterate();
  bool OnConnectToServer();
  bool OnStartUp();
  bool buildReport();
  void RegisterVariables();
  
 protected:
  bool handleMailNewSwimmer(std::string);
  bool handleMailFoundSwimmer(std::string);
  bool handleMailRescueRegion(std::string);
  void postShortestPath();
  void postNullPath();

  // Competitor-awareness (Option A): parse the rival's NODE_REPORT, score how
  // much each swimmer is "ours" to go for, and concede the clearly-lost ones.
  bool   handleMailNodeReport(std::string);
  double rivalWeight(std::string id);
  void   updateWeights();

  // #1 Cheapest insertion: slot one swimmer id into m_tour at the position
  // that adds the least travel distance, so a mid-mission swimmer joins the
  // existing route without forcing a disruptive full rebuild / U-turn.
  void   cheapestInsert(std::string id);

  // #2 Refine the order of m_tour in place with 2-opt (un-cross legs) and
  // Or-opt (relocate short chains). Only ever accepts a strictly shorter
  // tour, so it can never drop or duplicate a swimmer.
  void   optimizeTour();

  // Total travel length of an ordered swimmer-id list, starting from ownship.
  double tourLength(const std::vector<std::string>& order);

  // Old planning method (greedy nearest-neighbour ordering + 2-opt), kept so
  // the planner can compute it alongside the new method and drive whichever
  // tour is shorter -- guarantees the route is never worse than before.
  std::vector<std::string> greedyTour(const std::set<std::string>& skip);
  void   twoOptIds(std::vector<std::string>& order);

 private: // Config variables
  std::string m_vname;
  
 private: // State variables
  XYSegList  m_path;
  double     m_nav_x;
  double     m_nav_y;
  bool       m_nav_x_set;
  bool       m_nav_y_set;

  // Rival boat, learned from incoming NODE_REPORT (NAME != our own vname).
  double m_rival_x;
  double m_rival_y;
  double m_rival_hdg;
  double m_rival_spd;
  double m_rival_utc;   // MOOSTime of last rival report (for staleness)
  bool   m_rival_set;

  // Per-swimmer ownership weight in [0,1] (1 = fully ours). Smoothed.
  std::map<std::string, double> m_weight;
  // Swimmers we are currently conceding to the rival (hysteresis-controlled).
  std::set<std::string> m_conceded;

  // Lab 9 (Assignment 4): the swimmers we currently know about, keyed by
  // their unique swimmer id. Using a map makes the repeated (every ~15s)
  // SWIMMER_ALERT messages trivial to dedupe -- a known id is just ignored.
  std::map<std::string, XYPoint> m_swimmers;

  // Ids of swimmers already rescued. SWIMMER_ALERT messages keep repeating
  // even after a swimmer is rescued, so we must remember rescued ids
  // permanently and never re-add them to the target list above.
  std::set<std::string> m_rescued;

  // True whenever the swimmer set has changed (a swimmer was added or
  // rescued) and the rescue path therefore needs to be (re)built.
  bool         m_path_pending;

  // Running count of swimmers we were tracking that got rescued
  // (reported via FOUND_SWIMMER). Used in the AppCast report.
  unsigned int m_total_rescued;

  // Ordered swimmer ids of the current rescue plan (parallel to m_path).
  // Kept across replans so a new swimmer can be slotted into the existing
  // route (cheapest insertion) instead of rebuilding the tour from scratch.
  std::vector<std::string> m_tour;
};

#endif 
