/****************************************************************/
/*   NAME: Karolos Geroulanos                                             */
/*   ORGN: MIT, Cambridge MA                                    */
/*   FILE: PointAssign_Info.cpp                               */
/*   DATE: December 29th, 1963                                  */
/****************************************************************/

#include <cstdlib>
#include <iostream>
#include "PointAssign_Info.h"
#include "ColorParse.h"
#include "ReleaseInfo.h"

using namespace std;

//----------------------------------------------------------------
// Procedure: showSynopsis

void showSynopsis()
{
  blk("SYNOPSIS:                                                       ");
  blk("------------------------------------                            ");
  blk("  The pPointAssign application runs on the shoreside. It       ");
  blk("  receives VISIT_POINT postings (a burst of 100 points,         ");
  blk("  bracketed by firstpoint/lastpoint cues) and distributes       ");
  blk("  them across the configured vehicles, either alternating       ");
  blk("  or split by region (west/east). Each assigned point is        ");
  blk("  forwarded to VISIT_POINT_<VNAME> and drawn as a colored       ");
  blk("  VIEW_POINT for pMarineViewer.                                 ");
}

//----------------------------------------------------------------
// Procedure: showHelpAndExit

void showHelpAndExit()
{
  blk("                                                                ");
  blu("=============================================================== ");
  blu("Usage: pPointAssign file.moos [OPTIONS]                   ");
  blu("=============================================================== ");
  blk("                                                                ");
  showSynopsis();
  blk("                                                                ");
  blk("Options:                                                        ");
  mag("  --alias","=<ProcessName>                                      ");
  blk("      Launch pPointAssign with the given process name         ");
  blk("      rather than pPointAssign.                           ");
  mag("  --example, -e                                                 ");
  blk("      Display example MOOS configuration block.                 ");
  mag("  --help, -h                                                    ");
  blk("      Display this help message.                                ");
  mag("  --interface, -i                                               ");
  blk("      Display MOOS publications and subscriptions.              ");
  mag("  --version,-v                                                  ");
  blk("      Display the release version of pPointAssign.        ");
  blk("                                                                ");
  blk("Note: If argv[2] does not otherwise match a known option,       ");
  blk("      then it will be interpreted as a run alias. This is       ");
  blk("      to support pAntler launching conventions.                 ");
  blk("                                                                ");
  exit(0);
}

//----------------------------------------------------------------
// Procedure: showExampleConfigAndExit

void showExampleConfigAndExit()
{
  blk("                                                                ");
  blu("=============================================================== ");
  blu("pPointAssign Example MOOS Configuration                   ");
  blu("=============================================================== ");
  blk("                                                                ");
  blk("ProcessConfig = pPointAssign                              ");
  blk("{                                                               ");
  blk("  AppTick   = 4                                                 ");
  blk("  CommsTick = 4                                                 ");
  blk("                                                                ");
  blk("  vname = henry            // one line per vehicle              ");
  blk("  vname = gilda            // (builds the distribute list)      ");
  blk("                                                                ");
  blk("  assign_by_region = false // false=alternating, true=W/E split ");
  blk("  region_split_x   = 87.5  // x boundary for region mode        ");
  blk("}                                                               ");
  blk("                                                                ");
  exit(0);
}


//----------------------------------------------------------------
// Procedure: showInterfaceAndExit

void showInterfaceAndExit()
{
  blk("                                                                ");
  blu("=============================================================== ");
  blu("pPointAssign INTERFACE                                    ");
  blu("=============================================================== ");
  blk("                                                                ");
  showSynopsis();
  blk("                                                                ");
  blk("SUBSCRIPTIONS:                                                  ");
  blk("------------------------------------                            ");
  blk("  VISIT_POINT = x=24,y=-68,id=5                                 ");
  blk("  VISIT_POINT = firstpoint  (or)  lastpoint                     ");
  blk("                                                                ");
  blk("PUBLICATIONS:                                                   ");
  blk("------------------------------------                            ");
  blk("  VISIT_POINT_<VNAME> = x=24,y=-68,id=5  (forwarded spec)       ");
  blk("  VISIT_POINT_<VNAME> = firstpoint / lastpoint                  ");
  blk("  VIEW_POINT          = x=24,y=-68,label=henry_5,...            ");
  blk("                                                                ");
  exit(0);
}

//----------------------------------------------------------------
// Procedure: showReleaseInfoAndExit

void showReleaseInfoAndExit()
{
  showReleaseInfo("pPointAssign", "gpl");
  exit(0);
}

