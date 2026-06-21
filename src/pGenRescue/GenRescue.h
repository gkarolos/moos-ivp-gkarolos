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

  // Improve a greedy tour by repeatedly un-crossing pairs of legs (2-opt).
  // Only ever reorders the same vertices and only accepts strictly shorter
  // tours, so it can never drop a swimmer or lengthen the path.
  XYSegList twoOptPath(XYSegList segl, double sx, double sy);

 private: // Config variables
  std::string m_vname;
  
 private: // State variables
  XYSegList  m_path;
  double     m_nav_x;
  double     m_nav_y;
  bool       m_nav_x_set;
  bool       m_nav_y_set;

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
};

#endif 
