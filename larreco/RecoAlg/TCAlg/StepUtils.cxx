#include "larreco/RecoAlg/TCAlg/StepUtils.h"


struct SortEntry{
  unsigned int index;
  float val;
};

bool valsDecreasing (SortEntry c1, SortEntry c2) { return (c1.val > c2.val);}
//bool valIncreasing (SortEntry c1, SortEntry c2) { return (c1.val < c2.val);}

namespace tca {
  
  //////////////////////////////////////////
  void StepAway(TCSlice& slc, Trajectory& tj)
  {
    // Step along the direction specified in the traj vector in steps of size step
    // (wire spacing equivalents). Find hits between the last trajectory point and
    // the last trajectory point + step. A new trajectory point is added if hits are
    // found. Stepping continues until no signal is found for two consecutive steps
    // or until a wire or time boundary is reached.
    
    tj.IsGood = false;
    if(tj.Pts.empty()) return;
    
    unsigned short plane = DecodeCTP(tj.CTP).Plane;
    
    unsigned short lastPtWithUsedHits = tj.EndPt[1];
    
    unsigned short lastPt = lastPtWithUsedHits;
    // Construct a local TP from the last TP that will be moved on each step.
    // Only the Pos and Dir variables will be used
    TrajPoint ltp;
    ltp.CTP = tj.CTP;
    ltp.Pos = tj.Pts[lastPt].Pos;
    ltp.Dir = tj.Pts[lastPt].Dir;
    // A second TP is cloned from the leading TP of tj, updated with hits, fit
    // parameters,etc and possibly pushed onto tj as the next TP
    TrajPoint tp ;
    
    // assume it is good from here on
    tj.IsGood = true;
    
    unsigned short nMissedSteps = 0;
    
    for(unsigned short step = 1; step < 10000; ++step) {
      // make a copy of the previous TP
      lastPt = tj.Pts.size() - 1;
      tp = tj.Pts[lastPt];
      ++tp.Step;
      double stepSize = tcc.VLAStepSize;
      if(tp.AngleCode < 2) stepSize = std::abs(1/ltp.Dir[0]);
      // move the local TP position by one step in the right direction
      for(unsigned short iwt = 0; iwt < 2; ++iwt) ltp.Pos[iwt] += ltp.Dir[iwt] * stepSize;
      
      unsigned short ivx = TPNearVertex(slc, ltp);
      if(ivx != USHRT_MAX) {
        // Trajectory stops near a vertex so make the assignment
        if(AttachTrajToVertex(slc, tj, slc.vtxs[ivx], tcc.dbgStp || tcc.dbg2V)) tj.StopFlag[1][kAtVtx] = true;
        break;
      }
      
      SetPDGCode(slc, tj);
      
      // copy this position into tp
      tp.Pos = ltp.Pos;
      tp.Dir = ltp.Dir;
      if(tcc.dbgStp) {
        mf::LogVerbatim("TC")<<"StepAway "<<step<<" Pos "<<tp.Pos[0]<<" "<<tp.Pos[1]<<" Dir "<<tp.Dir[0]<<" "<<tp.Dir[1]<<" stepSize "<<stepSize<<" AngCode "<<tp.AngleCode;
      }
      // hit the boundary of the TPC?
      if(tp.Pos[0] < 0 || tp.Pos[0] > tcc.maxPos0[plane] ||
         tp.Pos[1] < 0 || tp.Pos[1] > tcc.maxPos1[plane]) break;
      // remove the old hits and other stuff
      tp.Hits.clear();
      tp.UseHit.reset();
      tp.FitChi = 0; tp.Chg = 0;
      // append to the trajectory
      tj.Pts.push_back(tp);
      // update the index of the last TP
      lastPt = tj.Pts.size() - 1;
      // look for hits
      bool sigOK = false;
      AddHits(slc, tj, lastPt, sigOK);
      // Check the stop flag
      if(tj.StopFlag[1][kAtTj]) break;
      // If successfull, AddHits has defined UseHit for this TP,
      // set the trajectory endpoints, and define HitPos.
      if(tj.Pts[lastPt].Hits.empty()) {
        // Require three points with charge on adjacent wires for small angle
        // stepping.
        if(tj.Pts[lastPt].AngleCode == 0 && lastPt == 2) return;
        // No close hits added.
        ++nMissedSteps;
        // First check for no signal in the vicinity
        if(lastPt > 0) {
          // break if this is a reverse propagate activity and there was no signal (not on a dead wire)
          if(!sigOK && tj.AlgMod[kRvPrp]) break;
          // Ensure that there is a signal here after missing a number of steps on a LA trajectory
          if(tj.Pts[lastPt].AngleCode > 0 && nMissedSteps > 4 && !SignalAtTp(slc, ltp)) {
            tj.StopFlag[1][kSignal] = false;
            break;
          }
          // the last point with hits (used or not) is the previous point
          unsigned short lastPtWithHits = lastPt - 1;
          float tps = TrajPointSeparation(tj.Pts[lastPtWithHits], ltp);
          float dwc = DeadWireCount(slc, ltp, tj.Pts[lastPtWithHits]);
          float nMissedWires = tps * std::abs(ltp.Dir[0]) - dwc;
          float maxWireSkip = tcc.maxWireSkipNoSignal;
          if(tj.PDGCode == 13) maxWireSkip = tcc.muonTag[2];
          if(tcc.dbgStp) mf::LogVerbatim("TC")<<" StepAway: no signal at ltp "<<PrintPos(slc, ltp)<<" nMissedWires "<<std::fixed<<std::setprecision(1)<<nMissedWires<<" dead wire count "<<dwc<<" maxWireSkip "<<maxWireSkip<<" tj.PGDCode "<<tj.PDGCode;
          if(nMissedWires > maxWireSkip) {
            // We passed a number of wires without adding hits and are ready to quit.
            // First see if there is one good unused hit on the end TP and if so use it
            // lastPtWithHits + 1 == lastPt && tj.Pts[lastPtWithHits].Chg == 0 && tj.Pts[lastPtWithHits].Hits.size() == 1
            if(tj.EndPt[1] < tj.Pts.size() - 1 && tj.Pts[tj.EndPt[1]+1].Hits.size() == 1) {
              unsigned short lastLonelyPoint = tj.EndPt[1] + 1;
              unsigned int iht = tj.Pts[lastLonelyPoint].Hits[0];
              if(slc.slHits[iht].InTraj == 0 && tj.Pts[lastLonelyPoint].Delta < 3 * tj.Pts[lastLonelyPoint].DeltaRMS) {
                slc.slHits[iht].InTraj = tj.ID;
                tj.Pts[lastLonelyPoint].UseHit[0] = true;
                DefineHitPos(slc, tj.Pts[lastLonelyPoint]);
                SetEndPoints(tj);
                if(tcc.dbgStp) {
                  mf::LogVerbatim("TC")<<" Added a Last Lonely Hit before breaking ";
                  PrintTrajPoint("LLH", slc, lastPt, tj.StepDir, tj.Pass, tj.Pts[lastLonelyPoint]);
                }
              }
            }
            break;
          }
        } // lastPt > 0
        // no sense keeping this TP on tj if no hits were added
        tj.Pts.pop_back();
        continue;
      } // tj.Pts[lastPt].Hits.empty()
      // ensure that we actually moved
      if(lastPt > 0 && PosSep2(tj.Pts[lastPt].Pos, tj.Pts[lastPt-1].Pos) < 0.1) return;
      // Found hits at this location so reset the missed steps counter
      nMissedSteps = 0;
      // Update the last point fit, etc using the just added hit(s)
      UpdateTraj(slc, tj);
      // a failure occurred
      if(tj.NeedsUpdate) return;
      if(tj.Pts[lastPt].Chg == 0) {
        // There are points on the trajectory by none used in the last step. See
        // how long this has been going on
        float tps = TrajPointSeparation(tj.Pts[tj.EndPt[1]], ltp);
        float dwc = DeadWireCount(slc, ltp, tj.Pts[tj.EndPt[1]]);
        float nMissedWires = tps * std::abs(ltp.Dir[0]) - dwc;
        if(tcc.dbgStp)  mf::LogVerbatim("TC")<<" Hits exist on the trajectory but are not used. Missed wires "<<std::nearbyint(nMissedWires)<<" dead wire count "<<(int)dwc;
        // break if this is a reverse propagate activity with no dead wires
        if(tj.AlgMod[kRvPrp] && dwc == 0) break;
        if(nMissedWires > tcc.maxWireSkipWithSignal) break;
        // try this out
        if(!MaskedHitsOK(slc, tj)) {
          return;
        }
        // check for a series of bad fits and stop stepping
        if(tcc.useAlg[kStopBadFits] && nMissedWires > 4 && StopIfBadFits(slc, tj)) break;
        // Keep stepping
        if(tcc.dbgStp) {
          if(tj.AlgMod[kRvPrp]) {
            PrintTrajectory("RP", slc, tj, lastPt);
          } else {
            PrintTrajectory("SC", slc, tj, lastPt);
          }
        }
        continue;
      } // tp.Hits.empty()
      if(tj.Pts.size() == 3) {
        // ensure that the last hit added is in the same direction as the first two.
        // This is a simple way of doing it
        bool badTj = (PosSep2(tj.Pts[0].HitPos, tj.Pts[2].HitPos) < PosSep2(tj.Pts[0].HitPos, tj.Pts[1].HitPos));
        // ensure that this didn't start as a small angle trajectory and immediately turn
        // into a large angle one
        if(!badTj && tj.Pts[lastPt].AngleCode > tcc.maxAngleCode[tj.Pass]) badTj = true;
        // check for a large change in angle
        if(!badTj) {
          float dang = DeltaAngle(tj.Pts[0].Ang, tj.Pts[2].Ang);
          if(dang > 0.5) badTj = false;
        }
        //check for a wacky delta
        if(!badTj && tj.Pts[2].Delta > 2) badTj = true;
        if(badTj) {
          if(tcc.dbgStp) mf::LogVerbatim("TC")<<" Bad Tj found on the third point. Quit stepping.";
          tj.IsGood = false;
          return;
        }
      } // tj.Pts.size() == 3
      // Update the local TP with the updated position and direction
      ltp.Pos = tj.Pts[lastPt].Pos;
      ltp.Dir = tj.Pts[lastPt].Dir;
      if(tj.MaskedLastTP) {
        // see if TPs have been masked off many times and if the
        // environment is clean. If so, return and try with next pass
        // cuts
        if(!MaskedHitsOK(slc, tj)) {
          if(tcc.dbgStp) {
            if(tj.AlgMod[kRvPrp]) {
              PrintTrajectory("RP", slc, tj, lastPt);
            } else {
              PrintTrajectory("SC", slc, tj, lastPt);
            }
          }
          return;
        }
        // Don't bother with the rest of the checking below if we
        // set all hits not used on this TP
        if(tcc.dbgStp) {
          if(tj.AlgMod[kRvPrp]) {
            PrintTrajectory("RP", slc, tj, lastPt);
          } else {
            PrintTrajectory("SC", slc, tj, lastPt);
          }
        }
        continue;
      }
      // We have added a TP with hits
      // assume that we aren't going to kill the point we just added, or any
      // of the previous points...
      unsigned short killPts = 0;
      // assume that we should keep going after killing points
      bool keepGoing = true;
      // check for a kink. Stop crawling if one is found
      GottaKink(slc, tj, killPts);
      if(tj.StopFlag[1][kAtKink]) keepGoing = false;
      // See if the Chisq/DOF exceeds the maximum.
      // UpdateTraj should have reduced the number of points fit
      // as much as possible for this pass, so this trajectory is in trouble.
      if(killPts == 0 &&  tj.Pts[lastPt].FitChi > tcc.maxChi && tj.PDGCode != 13) {
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<"   bad FitChi "<<tj.Pts[lastPt].FitChi<<" cut "<<tcc.maxChi;
        if(tcc.useAlg[kNewStpCuts]) {
          // remove the last point before quitting
          UnsetUsedHits(slc, tj.Pts[lastPt]);
          SetEndPoints(tj);
        }
        tj.IsGood = (NumPtsWithCharge(slc, tj, true) > tcc.minPtsFit[tj.Pass]);
        return;
      }
      // print the local tp unless we have killing to do
      if(killPts == 0) {
        if(tcc.dbgStp) {
          if(tj.AlgMod[kRvPrp]) {
            PrintTrajectory("RP", slc, tj, lastPt);
          } else {
            PrintTrajectory("SC", slc, tj, lastPt);
          }
        }
      } else {
        MaskTrajEndPoints(slc, tj, killPts);
        if(!tj.IsGood) return;
        unsigned int onWire = (float)(std::nearbyint(tj.Pts[lastPt].Pos[0]));
        float nSteps = (float)(step - tj.Pts[lastPt - killPts].Step);
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<"TRP   killing "<<killPts<<" after "<<nSteps<<" steps from prev TP.  Current tp.Pos "<<tp.Pos[0]<<" "<<tp.Pos[1];
        // move the position
        tj.Pts[lastPt].Pos[0] += nSteps * tj.Pts[lastPt].Dir[0];
        tj.Pts[lastPt].Pos[1] += nSteps * tj.Pts[lastPt].Dir[1];
        if(tj.Pts[lastPt].AngleCode == 0) {
          // put the TP at the wire position prior to the move
          float dw = onWire - tj.Pts[lastPt].Pos[0];
          tj.Pts[lastPt].Pos[0] = onWire;
          tj.Pts[lastPt].Pos[1] += dw * tj.Pts[lastPt].Dir[1] / tj.Pts[lastPt].Dir[0];
        }
        // check the MCSMom after we going
        if(tj.Pts.size() > 20 && tj.Pass < tcc.minMCSMom.size() && tj.MCSMom < tcc.minMCSMom[tj.Pass]) break;
        // copy to the local trajectory point
        ltp.Pos = tj.Pts[lastPt].Pos;
        ltp.Dir = tj.Pts[lastPt].Dir;
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<"  New ltp.Pos     "<<ltp.Pos[0]<<" "<<ltp.Pos[1]<<" ticks "<<(int)ltp.Pos[1]/tcc.unitsPerTick;
        if(!keepGoing) break;
      }
    } // step
    
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<"End StepAway with tj size "<<tj.Pts.size()<<" isGood = "<<tj.IsGood;
    
  } // StepAway

  //////////////////////////////////////////
  void UpdateTraj(TCSlice& slc, Trajectory& tj)
  {
    // Updates the last added trajectory point fit, average hit rms, etc.
    
    tj.NeedsUpdate = true;
    tj.MaskedLastTP = false;
    
    if(tj.EndPt[1] < 1) return;
    unsigned int lastPt = tj.EndPt[1];
    TrajPoint& lastTP = tj.Pts[lastPt];
    
    // find the previous TP that has hits (and was therefore in the fit)
    unsigned short prevPtWithHits = USHRT_MAX;
    unsigned short firstFitPt = tj.EndPt[0];
    for(unsigned short ii = 1; ii < tj.Pts.size(); ++ii) {
      unsigned short ipt = lastPt - ii;
      if(tj.Pts[ipt].Chg > 0) {
        prevPtWithHits = ipt;
        break;
      }
      if(ipt == 0) break;
    } // ii
    if(prevPtWithHits == USHRT_MAX) return;
    
    // define the FitChi threshold above which something will be done
    float maxChi = 2;
    unsigned short minPtsFit = tcc.minPtsFit[tj.Pass];
    // just starting out?
    if(lastPt < 4) minPtsFit = 2;
    // was !TrajIsClean...
    if(tj.PDGCode == 13 && TrajIsClean(slc, tj, tcc.dbgStp)) {
      // Fitting a clean muon
      maxChi = tcc.maxChi;
      minPtsFit = lastPt / 3;
    }
    
    // Set the lastPT delta before doing the fit
    lastTP.Delta = PointTrajDOCA(slc, lastTP.HitPos[0], lastTP.HitPos[1], lastTP);
    
    // update MCSMom. First ensure that nothing bad has happened
    float newMCSMom = MCSMom(slc, tj);
    if(lastPt > 10 && newMCSMom < 0.6 * tj.MCSMom) {
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<"UpdateTraj: MCSMom took a nose-dive "<<newMCSMom;
      UnsetUsedHits(slc, lastTP);
      DefineHitPos(slc, lastTP);
      SetEndPoints(tj);
      tj.NeedsUpdate = false;
      return;
    }
    tj.MCSMom = newMCSMom;
    
    if(tcc.dbgStp) {
      mf::LogVerbatim("TC")<<"UpdateTraj: lastPt "<<lastPt<<" lastTP.Delta "<<lastTP.Delta<<" previous point with hits "<<prevPtWithHits<<" tj.Pts size "<<tj.Pts.size()<<" AngleCode "<<lastTP.AngleCode<<" PDGCode "<<tj.PDGCode<<" maxChi "<<maxChi<<" minPtsFit "<<minPtsFit<<" MCSMom "<<tj.MCSMom;
    }
    
    UpdateTjChgProperties("UT", slc, tj, tcc.dbgStp);
    
    if(lastPt == 1) {
      // Handle the second trajectory point. No error calculation. Just update
      // the position and direction
      lastTP.NTPsFit = 2;
      FitTraj(slc, tj);
      lastTP.FitChi = 0.01;
      lastTP.AngErr = tj.Pts[0].AngErr;
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<"UpdateTraj: Second traj point pos "<<lastTP.Pos[0]<<" "<<lastTP.Pos[1]<<"  dir "<<lastTP.Dir[0]<<" "<<lastTP.Dir[1];
      tj.NeedsUpdate = false;
      SetAngleCode(lastTP);
      return;
    }
    
    if(lastPt == 2) {
      // Third trajectory point. Keep it simple
      lastTP.NTPsFit = 3;
      FitTraj(slc, tj);
      tj.NeedsUpdate = false;
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<"UpdateTraj: Third traj point fit "<<lastTP.FitChi;
      SetAngleCode(lastTP);
      return;
    }
    
    // Fit with > 2 TPs
    // Keep adding hits until Chi/DOF exceeds 1
    if(tj.Pts[prevPtWithHits].FitChi < 1) lastTP.NTPsFit += 1;
    // Reduce the number of points fit if the trajectory is long and chisq is getting a bit larger
    if(lastPt > 20 && tj.Pts[prevPtWithHits].FitChi > 1.5 && lastTP.NTPsFit > minPtsFit) lastTP.NTPsFit -= 2;
    
    FitTraj(slc, tj);
    
    // don't get too fancy when we are starting out
    if(lastPt < 6) {
      tj.NeedsUpdate = false;
      UpdateDeltaRMS(slc, tj);
      SetAngleCode(lastTP);
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<" Return with lastTP.FitChi "<<lastTP.FitChi<<" Chg "<<lastTP.Chg;
      return;
    }
    
    // find the first point that was fit.
    unsigned short cnt = 0;
    for(unsigned short ii = 0; ii < tj.Pts.size(); ++ii) {
      unsigned short ipt = lastPt - ii;
      if(tj.Pts[ipt].Chg > 0) {
        firstFitPt = ipt;
        ++cnt;
      }
      if(cnt == lastTP.NTPsFit) break;
      if(ipt == 0) break;
    }
    
    unsigned short ndead = DeadWireCount(slc, lastTP.HitPos[0], tj.Pts[firstFitPt].HitPos[0], tj.CTP);
    
    if(lastTP.FitChi > 1.5 && tj.Pts.size() > 6) {
      // A large chisq jump can occur if we just jumped a large block of dead wires. In
      // this case we don't want to mask off the last TP but reduce the number of fitted points
      // This count will be off if there a lot of dead or missing wires...
      // reduce the number of points significantly
      if(ndead > 5) {
        if(lastTP.NTPsFit > 5) lastTP.NTPsFit = 5;
      } else {
        // Have a longish trajectory and chisq was a bit large.
        // Was this a sudden occurrence and the fraction of TPs are included
        // in the fit? If so, we should mask off this
        // TP and keep going. If these conditions aren't met, we
        // should reduce the number of fitted points
        float chirat = 0;
        if(prevPtWithHits != USHRT_MAX && tj.Pts[prevPtWithHits].FitChi > 0) chirat = lastTP.FitChi / tj.Pts[prevPtWithHits].FitChi;
        // Don't mask hits when doing RevProp. Reduce NTPSFit instead
        tj.MaskedLastTP = (chirat > 1.5 && lastTP.NTPsFit > 0.3 * NumPtsWithCharge(slc, tj, false) && !tj.AlgMod[kRvPrp]);
        // BB April 19, 2018: Don't mask TPs on low MCSMom Tjs
        if(tj.MaskedLastTP && tj.MCSMom < 30) tj.MaskedLastTP = false;
        if(tcc.dbgStp) {
          mf::LogVerbatim("TC")<<" First fit chisq too large "<<lastTP.FitChi<<" prevPtWithHits chisq "<<tj.Pts[prevPtWithHits].FitChi<<" chirat "<<chirat<<" NumPtsWithCharge "<<NumPtsWithCharge(slc, tj, false)<<" tj.MaskedLastTP "<<tj.MaskedLastTP;
        }
        // we should also mask off the last TP if there aren't enough hits
        // to satisfy the minPtsFit constraint
        if(!tj.MaskedLastTP && NumPtsWithCharge(slc, tj, true) < minPtsFit) tj.MaskedLastTP = true;
      } // few dead wires
    } // lastTP.FitChi > 2 ...
    
    // Deal with a really long trajectory that is in trouble (uB cosmic).
    if(tj.PDGCode == 13 && lastTP.FitChi > tcc.maxChi) {
      if(lastTP.NTPsFit > 1.3 * tcc.muonTag[0]) {
        lastTP.NTPsFit *= 0.8;
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<" Muon - Reduce NTPsFit "<<lastPt;
      } else {
        tj.MaskedLastTP = true;
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<" Muon - mask last point "<<lastPt;
      }
    }
    
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<"UpdateTraj: First fit "<<lastTP.Pos[0]<<" "<<lastTP.Pos[1]<<"  dir "<<lastTP.Dir[0]<<" "<<lastTP.Dir[1]<<" FitChi "<<lastTP.FitChi<<" NTPsFit "<<lastTP.NTPsFit<<" ndead wires "<<ndead<<" tj.MaskedLastTP "<<tj.MaskedLastTP;
    if(tj.MaskedLastTP) {
      UnsetUsedHits(slc, lastTP);
      DefineHitPos(slc, lastTP);
      SetEndPoints(tj);
      lastPt = tj.EndPt[1];
      lastTP.NTPsFit -= 1;
      FitTraj(slc, tj);
      UpdateTjChgProperties("UT", slc, tj, tcc.dbgStp);
      SetAngleCode(lastTP);
      return;
    }  else {
      // a more gradual change in chisq. Maybe reduce the number of points
      unsigned short newNTPSFit = lastTP.NTPsFit;
      // reduce the number of points fit to keep Chisq/DOF < 2 adhering to the pass constraint
      // and also a minimum number of points fit requirement for long muons
      float prevChi = lastTP.FitChi;
      unsigned short ntry = 0;
      float chiCut = 1.5;
      while(lastTP.FitChi > chiCut && lastTP.NTPsFit > minPtsFit) {
        if(lastTP.NTPsFit > 15) {
          newNTPSFit = 0.7 * newNTPSFit;
        } else if(lastTP.NTPsFit > 4) {
          newNTPSFit -= 2;
        } else {
          newNTPSFit -= 1;
        }
        if(lastTP.NTPsFit < 3) newNTPSFit = 2;
        if(newNTPSFit < minPtsFit) newNTPSFit = minPtsFit;
        lastTP.NTPsFit = newNTPSFit;
        // BB April 19: try to add a last lonely hit on a low MCSMom tj on the last try
        if(newNTPSFit == minPtsFit && tj.MCSMom < 30) chiCut = 2;
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<"  Bad FitChi "<<lastTP.FitChi<<" Reduced NTPsFit to "<<lastTP.NTPsFit<<" Pass "<<tj.Pass<<" chiCut "<<chiCut;
        FitTraj(slc, tj);
        tj.NeedsUpdate = true;
        if(lastTP.FitChi > prevChi) {
          if(tcc.dbgStp) mf::LogVerbatim("TC")<<"  Chisq is increasing "<<lastTP.FitChi<<"  Try to remove an earlier bad hit";
          MaskBadTPs(slc, tj, chiCut);
          ++ntry;
          if(ntry == 2) break;
        }
        prevChi = lastTP.FitChi;
        if(lastTP.NTPsFit == minPtsFit) break;
      } // lastTP.FitChi > 2 && lastTP.NTPsFit > 2
    }
    
    // last ditch attempt if things look bad. Drop the last hit
    if(tj.Pts.size() > tcc.minPtsFit[tj.Pass] && lastTP.FitChi > maxChi) {
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<"  Last try. Drop last TP "<<lastTP.FitChi<<" NTPsFit "<<lastTP.NTPsFit;
      UnsetUsedHits(slc, lastTP);
      DefineHitPos(slc, lastTP);
      SetEndPoints(tj);
      lastPt = tj.EndPt[1];
      FitTraj(slc, tj);
      tj.MaskedLastTP = true;
    }
    
    if(tj.NeedsUpdate) UpdateTjChgProperties("UT", slc, tj, tcc.dbgStp);
    
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<"  Fit done. Chi "<<lastTP.FitChi<<" NTPsFit "<<lastTP.NTPsFit;
    
    if(tj.EndPt[0] == tj.EndPt[1]) return;
    
    // Don't let the angle error get too small too soon. Stepping would stop if the first
    // few hits on a low momentum wandering track happen to have a very good fit to a straight line.
    // We will do this by averaging the default starting value of AngErr of the first TP with the current
    // value from FitTraj.
    if(lastPt < 14) {
      float defFrac = 1 / (float)(tj.EndPt[1]);
      lastTP.AngErr = defFrac * tj.Pts[0].AngErr + (1 - defFrac) * lastTP.AngErr;
    }
    
    UpdateDeltaRMS(slc, tj);
    SetAngleCode(lastTP);
    
    tj.NeedsUpdate = false;
    return;
    
  } // UpdateTraj

  
  ////////////////////////////////////////////////
  void CheckTraj(TCSlice& slc, Trajectory& tj)
  {
    // Check the quality of the trajectory and possibly trim it or flag it for deletion
    
    if(!tj.IsGood) return;
    
    // ensure that the end points are defined
    SetEndPoints(tj);
    if(tj.EndPt[0] == tj.EndPt[1]) return;
    
    if(tcc.dbgStp) {
      mf::LogVerbatim("TC")<<"inside CheckTraj with NumPtsWithCharge = "<<NumPtsWithCharge(slc, tj, false);
    }
    
    if(NumPtsWithCharge(slc, tj, false) < tcc.minPts[tj.Pass]) {
      tj.IsGood = false;
      return;
    }
    
    // Look for a charge asymmetry between points on both sides of a high-
    // charge point and trim points in the vicinity
    ChkChgAsymmetry(slc, tj, tcc.dbgStp);
    
    // flag this tj as a junk Tj (even though it wasn't created in FindJunkTraj).
    // Drop it and let FindJunkTraj do it's job
    TagJunkTj(slc, tj, tcc.dbgStp);
    if(tj.AlgMod[kJunkTj]) {
      tj.IsGood = false;
      return;
    }
    
    tj.MCSMom = MCSMom(slc, tj);
    
    // See if the points at the stopping end can be included in the Tj
    ChkStopEndPts(slc, tj, tcc.dbgStp);
    
    // remove any points at the end that don't have charge
    tj.Pts.resize(tj.EndPt[1] + 1);
    
    // Ensure that a hit only appears once in the TJ
    if(HasDuplicateHits(slc, tj, tcc.dbgStp)) {
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<" HasDuplicateHits ";
      tj.IsGood = false;
      return;
    }
    
    // See if this is a ghost trajectory
    if(IsGhost(slc, tj)) {
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<" CT: Ghost trajectory - trimmed hits ";
      if(!tj.IsGood) return;
    }
    
    if(tj.AlgMod[kJunkTj]) return;
    
    // checks are different for Very Large Angle trajectories
    bool isVLA = (tj.Pts[tj.EndPt[1]].AngleCode == 2);
    // The last two ranges are Large Angle and Very Large Angle. Determine if the TJ is Small Angle
    bool isSA = (tj.Pts[tj.EndPt[1]].AngleCode == 0);
    
    // First remove any TPs at the end that have no hits after
    // setting the StopFlag. Assume that there are no hits on TPs after the end
    tj.StopFlag[1][kSignal] = false;
    if(tj.EndPt[1] < tj.Pts.size() - 1) {
      // There must be hits at the end so set the kSignal StopFlag
      if(!tj.Pts[tj.EndPt[1]+1].Hits.empty()) tj.StopFlag[1][kSignal] = true;
    }
    tj.Pts.resize(tj.EndPt[1] + 1);
    
    // Fill in any gaps with hits that were skipped, most likely delta rays on muon tracks
    if(!isVLA) FillGaps(slc, tj);
    
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<" CheckTraj MCSMom "<<tj.MCSMom<<" isVLA? "<<isVLA<<" NumPtsWithCharge "<<NumPtsWithCharge(slc, tj, false)<<" Min Req'd "<<tcc.minPts[tj.Pass];
    
    // Check for hit width consistency on short trajectories
    if(tj.Pts.size() < 10) {
      float maxWidth = 0;
      float minWidth = 999;
      for(unsigned short ipt = tj.EndPt[0]; ipt <= tj.EndPt[1]; ++ipt) {
        if(tj.Pts[ipt].Chg == 0) continue;
        if(tj.Pts[ipt].HitPosErr2 > maxWidth) maxWidth = tj.Pts[ipt].HitPosErr2;
        if(tj.Pts[ipt].HitPosErr2 < minWidth) minWidth = tj.Pts[ipt].HitPosErr2;
      } // ipt
      // Require less than a 3X difference in the hit width or 10X for HitPosErr2
      if(maxWidth > 10 * minWidth) {
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<" TP width variation too large: minWidth "<<minWidth<<" maxWidth "<<maxWidth;
        tj.IsGood = false;
        return;
      }
    } // short trajectory
    
    // Trim the end points until the TJ meets the quality cuts
    TrimEndPts("CT", slc, tj, tcc.qualityCuts, tcc.dbgStp);
    if(tj.AlgMod[kKilled]) {
      tj.IsGood = false;
      return;
    }
    
    // Check for a Bragg peak at both ends. This may be used by FixTrajBegin.
    ChkStop(slc, tj);
    
    // Update the trajectory parameters at the beginning of the trajectory
    FixTrajBegin(slc, tj);
    
    // ignore short trajectories
    if(tj.EndPt[1] < 4) return;
    
    if(isSA && !tj.StopFlag[1][kBragg]) {
      // Small angle checks
      
      if(tcc.useAlg[kCTKink] && tj.EndPt[1] > 8 && !tj.StopFlag[1][kAtKink] && tj.MCSMom > 50) {
        // look for the signature of a kink near the end of the trajectory.
        // These are: Increasing delta for the last few hits
        unsigned short newSize = USHRT_MAX;
        unsigned short lastPtToChk = tj.EndPt[1] - 4;
        float deltaCut = 2 * tj.Pts[lastPtToChk].DeltaRMS;
        for(unsigned short ipt = tj.EndPt[1]; ipt > lastPtToChk; --ipt) {
          // Stop checking if delta is good
          if(tj.Pts[ipt].Delta < deltaCut) break;
          float drat = tj.Pts[ipt].Delta / tj.Pts[ipt-1].Delta;
          if(drat > 1.2) newSize = ipt;
        } // ipt
        if(newSize != USHRT_MAX) {
          if(tcc.dbgStp) mf::LogVerbatim("TC")<<"CTKink: Masking end points to newSize "<<newSize;
          for(unsigned short ipt = newSize; ipt < tj.Pts.size(); ++ipt) UnsetUsedHits(slc, tj.Pts[ipt]);
          SetEndPoints(tj);
          tj.AlgMod[kCTKink] = true;
        }
      } // tcc.useAlg[kCTKink]
      
      if(tcc.useAlg[kCTStepChk] && !tj.AlgMod[kRvPrp]) {
        // Compare the number of steps taken per TP near the beginning and
        // at the end. This will get confused if RevProp is used
        short nStepBegin = tj.Pts[2].Step - tj.Pts[1].Step;
        short nStepEnd;
        unsigned short lastPt = tj.Pts.size() - 1;
        unsigned short newSize = tj.Pts.size();
        for(unsigned short ipt = lastPt; ipt > lastPt - 2; --ipt) {
          nStepEnd = tj.Pts[ipt].Step - tj.Pts[ipt - 1].Step;
          if(nStepEnd > 3 * nStepBegin) newSize = ipt;
        }
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<"CTStepChk: check number of steps. newSize "<<newSize<<" tj.Pts.size() "<<tj.Pts.size();
        if(newSize < tj.Pts.size()) {
          for(unsigned short ipt = newSize; ipt < tj.Pts.size(); ++ipt) UnsetUsedHits(slc, tj.Pts[ipt]);
          SetEndPoints(tj);
          tj.AlgMod[kCTStepChk] = true;
          tj.Pts.resize(newSize);
          return;
        } // newSize < tj.Pts.size()
      } // tcc.useAlg[kCTStepChk]
    } // isSA
    
    FindSoftKink(slc, tj);
    
    HiEndDelta(slc, tj);
    
    // final quality check
    float npwc = NumPtsWithCharge(slc, tj, true);
    float npts = tj.EndPt[1] - tj.EndPt[0] + 1;
    float frac = npwc / npts;
    tj.IsGood = (frac >= tcc.qualityCuts[0]);
    if(tj.IsGood && tj.Pass < tcc.minMCSMom.size()) tj.IsGood = (tj.MCSMom >= tcc.minMCSMom[tj.Pass]);
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<"CTStepChk: fraction of points with charge "<<frac<<" good traj? "<<tj.IsGood;
    if(!tj.IsGood || !slc.isValid) return;
    
    // lop off high multiplicity hits at the end
    CheckHiMultEndHits(slc, tj);
    
    // Check for a Bragg peak at both ends. This may be used by FixTrajBegin.
    ChkStop(slc, tj);
    
    if(tcc.dbgStp && tj.Pts.size() < 100) PrintTrajectory("CTo", slc, tj, USHRT_MAX);
    
  } // CheckTraj

  ////////////////////////////////////////////////
  void AddHits(TCSlice& slc, Trajectory& tj, unsigned short ipt, bool& sigOK)
  {
    // Try to add hits to the trajectory point ipt on the supplied
    // trajectory
    
    // assume failure
    sigOK = false;
    
    if(tj.Pts.empty()) return;
    if(ipt > tj.Pts.size() - 1) return;
    
    // Call large angle hit finding if the last tp is large angle
    if(tj.Pts[ipt].AngleCode == 2) {
      AddLAHits(slc, tj, ipt, sigOK);
      return;
    }
    std::vector<unsigned int> closeHits;
    
    unsigned int lastPtWithUsedHits = tj.EndPt[1];
    TrajPoint& tp = tj.Pts[ipt];
    
    unsigned short plane = DecodeCTP(tj.CTP).Plane;
    unsigned int wire = std::nearbyint(tp.Pos[0]);
    if(wire < slc.firstWire[plane] || wire > slc.lastWire[plane]-1) return;
    // Move the TP to this wire
    MoveTPToWire(tp, (float)wire);
    
    // find the projection error to this point. Note that if this is the first
    // TP, lastPtWithUsedHits = 0, so the projection error is 0
    float dw = tp.Pos[0] - tj.Pts[lastPtWithUsedHits].Pos[0];
    float dt = tp.Pos[1] - tj.Pts[lastPtWithUsedHits].Pos[1];
    float dpos = sqrt(dw * dw + dt * dt);
    float projErr = dpos * tj.Pts[lastPtWithUsedHits].AngErr;
    // Add this to the Delta RMS factor and construct a cut
    float deltaCut = 3 * (projErr + tp.DeltaRMS);
    
    deltaCut *= tcc.projectionErrFactor;
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<" AddHits: calculated deltaCut "<<deltaCut<<" dw "<<dw<<" dpos "<<dpos;
    
    //    if(deltaCut < 2) deltaCut = 2;
    // Jan 26 Cut is too loose
    if(deltaCut < 0.5) deltaCut = 0.5;
    if(deltaCut > 3) deltaCut = 3;
    
    // TY: open it up for RevProp, since we might be following a stopping track
    if(tj.AlgMod[kRvPrp]) deltaCut *= 2;
    
    // loosen up a bit if we just passed a block of dead wires
    bool passedDeadWires = (abs(dw) > 20 && DeadWireCount(slc, tp.Pos[0], tj.Pts[lastPtWithUsedHits].Pos[0], tj.CTP) > 10);
    if(passedDeadWires) deltaCut *= 2;
    
    // Create a larger cut to use in case there is nothing close
    float bigDelta = 2 * deltaCut;
    unsigned int imBig = UINT_MAX;
    tp.Delta = deltaCut;
    // ignore all hits with delta larger than maxDeltaCut
    float maxDeltaCut = 2 * bigDelta;
    // apply some limits
    if(tcc.useAlg[kNewStpCuts] && !passedDeadWires && maxDeltaCut > 3) {
      maxDeltaCut = 3;
      bigDelta = 1.5;
    }
    
    // projected time in ticks for testing the existence of a hit signal
    raw::TDCtick_t rawProjTick = (float)(tp.Pos[1] / tcc.unitsPerTick);
    if(tcc.dbgStp) {
      mf::LogVerbatim("TC")<<" AddHits: wire "<<wire<<" tp.Pos[0] "<<tp.Pos[0]<<" projTick "<<rawProjTick<<" deltaRMS "<<tp.DeltaRMS<<" tp.Dir[0] "<<tp.Dir[0]<<" deltaCut "<<deltaCut<<" dpos "<<dpos<<" projErr "<<projErr<<" ExpectedHitsRMS "<<ExpectedHitsRMS(slc, tp);
    }
    
    std::vector<unsigned int> hitsInMultiplet;
    
    geo::PlaneID planeID = DecodeCTP(tj.CTP);
    unsigned int ipl = planeID.Plane;
    if(wire > slc.lastWire[ipl]) return;
    // Assume a signal exists on a dead wire
    if(slc.wireHitRange[ipl][wire].first == -1) sigOK = true;
    if(slc.wireHitRange[ipl][wire].first < 0) return;
    unsigned int firstHit = (unsigned int)slc.wireHitRange[ipl][wire].first;
    unsigned int lastHit = (unsigned int)slc.wireHitRange[ipl][wire].second;
    float fwire = wire;
    for(unsigned int iht = firstHit; iht < lastHit; ++iht) {
      if(slc.slHits[iht].InTraj == tj.ID) continue;
      if(slc.slHits[iht].InTraj == SHRT_MAX) continue;
      auto& hit = (*evt.allHits)[slc.slHits[iht].allHitsIndex];
      if(rawProjTick > hit.StartTick() && rawProjTick < hit.EndTick()) sigOK = true;
      float ftime = tcc.unitsPerTick * hit.PeakTime();
      float delta = PointTrajDOCA(slc, fwire, ftime, tp);
      if(delta > maxDeltaCut) continue;
      float dt = std::abs(ftime - tp.Pos[1]);
      unsigned short localIndex = 0;
      GetHitMultiplet(slc, iht, hitsInMultiplet, localIndex);
      if(tcc.dbgStp && delta < 100 && dt < 100) {
        mf::LogVerbatim myprt("TC");
        myprt<<"  iht "<<iht;
        myprt<<" "<<PrintHit(slc.slHits[iht]);
        myprt<<" delta "<<std::fixed<<std::setprecision(2)<<delta<<" deltaCut "<<deltaCut<<" dt "<<dt;
        myprt<<" BB Mult "<<hitsInMultiplet.size()<<" localIndex "<<localIndex<<" RMS "<<std::setprecision(1)<<hit.RMS();
        myprt<<" Chi "<<std::setprecision(1)<<hit.GoodnessOfFit();
        myprt<<" InTraj "<<slc.slHits[iht].InTraj;
        myprt<<" Chg "<<(int)hit.Integral();
        myprt<<" Signal? "<<sigOK;
      }
      if(slc.slHits[iht].InTraj == 0 && delta < bigDelta && hitsInMultiplet.size() < 3 && !tj.AlgMod[kRvPrp]) {
        // An available hit that is just outside the window that is not part of a large multiplet
        bigDelta = delta;
        imBig = iht;
      }
      if(delta > deltaCut) continue;
      if(std::find(closeHits.begin(), closeHits.end(), iht) != closeHits.end()) continue;
      closeHits.push_back(iht);
      if(hitsInMultiplet.size() > 1) {
        // include all the hits in a multiplet
        for(auto& jht : hitsInMultiplet) {
          if(slc.slHits[jht].InTraj == tj.ID) continue;
          if(std::find(closeHits.begin(), closeHits.end(), jht) != closeHits.end()) continue;
          closeHits.push_back(jht);
        } // jht
      } // multiplicity > 1
    } // iht
    
    if(tcc.dbgStp) {
      mf::LogVerbatim myprt("TC");
      myprt<<"closeHits ";
      for(auto iht : closeHits) myprt<<" "<<PrintHit(slc.slHits[iht]);
      if(imBig < slc.slHits.size()) {
        myprt<<" imBig "<<PrintHit(slc.slHits[imBig]);
      } else {
        myprt<<" imBig "<<imBig;
      }
    }
    if(closeHits.empty() && imBig == UINT_MAX) {
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<" no signal on any wire at tp.Pos "<<tp.Pos[0]<<" "<<tp.Pos[1]<<" tick "<<(int)tp.Pos[1]/tcc.unitsPerTick<<" closeHits size "<<closeHits.size();
      return;
    }
    if(imBig < slc.slHits.size() && closeHits.empty()) {
      closeHits.push_back(imBig);
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<" Added bigDelta hit "<<PrintHit(slc.slHits[imBig])<<" w delta = "<<bigDelta;
    }
    if(!closeHits.empty()) sigOK = true;
    if(!sigOK) return;
    tp.Hits.insert(tp.Hits.end(), closeHits.begin(), closeHits.end());
    if(tp.Hits.size() > 16) {
      // Actually this is a hopelessly messy region that we should ignore
      tp.Hits.clear();
      tp.Chg = 0;
      return;
    }
    // reset UseHit and assume that none of these hits will be used (yet)
    tp.UseHit.reset();
    // decide which of these hits should be used in the fit. Use a generous maximum delta
    // and require a charge check if we'not just starting out
    bool useChg = true;
    FindUseHits(slc, tj, ipt, 10, useChg);
    DefineHitPos(slc, tp);
    SetEndPoints(tj);
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<" number of close hits "<<closeHits.size()<<" used hits "<<NumHitsInTP(tp, kUsedHits);
  } // AddHits
  
  
  ////////////////////////////////////////////////
  void AddLAHits(TCSlice& slc, Trajectory& tj, unsigned short ipt, bool& sigOK)
  {
    // Very Large Angle version of AddHits to be called for the last angle range
    
    if(ipt > tj.Pts.size() - 1) return;
    TrajPoint& tp = tj.Pts[ipt];
    tp.Hits.clear();
    tp.UseHit.reset();
    sigOK = false;
    
    unsigned short plane = DecodeCTP(tj.CTP).Plane;
    
    // look at adjacent wires for larger angle trajectories
    // We will check the most likely wire first
    std::vector<int> wires(1);
    wires[0] = std::nearbyint(tp.Pos[0]);
    if(wires[0] < 0 || wires[0] > (int)slc.lastWire[plane]-1) return;
    
    if(tp.AngleCode != 2) {
      mf::LogVerbatim("TC")<<"AddLAHits called with a bad angle code. "<<tp.AngleCode<<" Don't do this";
      return;
    }
    // and the adjacent wires next in the most likely order only
    // after the first two points have been defined
    if(ipt > 1) {
      if(tp.Dir[0] > 0) {
        if(wires[0] < (int)slc.lastWire[plane]-1) wires.push_back(wires[0] + 1);
        if(wires[0] > 0) wires.push_back(wires[0] - 1);
      } else {
        if(wires[0] > 0) wires.push_back(wires[0] - 1);
        if(wires[0] < (int)slc.lastWire[plane]-1) wires.push_back(wires[0] + 1);
      }
    } // ipt > 0 ...
    
    if(tcc.dbgStp) {
      mf::LogVerbatim myprt("TC");
      myprt<<" AddLAHits: Pos "<<PrintPos(slc, tp)<<" tp.AngleCode "<<tp.AngleCode<<" Wires under consideration";
      for(auto& wire : wires) myprt<<" "<<wire;
    }
    
    // a temporary tp that we can move around
    TrajPoint ltp = tp;
    // do this while testing
    sigOK = false;
    
    tp.Hits.clear();
    std::array<int, 2> wireWindow;
    std::array<float, 2> timeWindow;
    float pos1Window = tcc.VLAStepSize/2;
    timeWindow[0] = ltp.Pos[1] - pos1Window;
    timeWindow[1] = ltp.Pos[1] + pos1Window;
    // Put the existing hits in to a vector so we can ensure that they aren't added again
    std::vector<unsigned int> oldHits = PutTrajHitsInVector(tj, kAllHits);
    
    for(unsigned short ii = 0; ii < wires.size(); ++ii) {
      int wire = wires[ii];
      if(wire < 0 || wire > (int)slc.lastWire[plane]) continue;
      // Assume a signal exists on a dead wire
      if(slc.wireHitRange[plane][wire].first == -1) sigOK = true;
      if(slc.wireHitRange[plane][wire].first < 0) continue;
      wireWindow[0] = wire;
      wireWindow[1] = wire;
      bool hitsNear;
      // Look for hits using the requirement that the timeWindow overlaps with the hit StartTick and EndTick
      std::vector<unsigned int> closeHits = FindCloseHits(slc, wireWindow, timeWindow, plane, kAllHits, true, hitsNear);
      if(hitsNear) sigOK = true;
      for(auto& iht : closeHits) {
        // Ensure that none of these hits are already used by this trajectory
        if(slc.slHits[iht].InTraj == tj.ID) continue;
        // or in another trajectory in any previously added point
        if(std::find(oldHits.begin(), oldHits.end(), iht) != oldHits.end()) continue;
        tp.Hits.push_back(iht);
      }
    } // ii
    
    if(tcc.dbgStp) {
      mf::LogVerbatim myprt("TC");
      myprt<<" LAPos "<<PrintPos(slc, ltp)<<" Tick window "<<(int)(timeWindow[0]/tcc.unitsPerTick)<<" to "<<(int)(timeWindow[1]/tcc.unitsPerTick);
      for(auto& iht : tp.Hits) myprt<<" "<<PrintHit(slc.slHits[iht]);
    } // prt
    
    // no hits found
    if(tp.Hits.empty()) return;
    
    if(tp.Hits.size() > 16) tp.Hits.resize(16);
    
    tp.UseHit.reset();
    
    if(tcc.useAlg[kStopAtTj]) {
      // don't continue if we have run into another trajectory that has a similar angle
      unsigned short nAvailable = 0;
      unsigned int otherTjHit = INT_MAX;
      for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) {
        auto& tcHit = slc.slHits[tp.Hits[ii]];
        if(tcHit.InTraj == SHRT_MAX) continue;
        if(tcHit.InTraj > 0) {
          otherTjHit = tp.Hits[ii];
          continue;
        }
        ++nAvailable;
      } // ii
      if(nAvailable == 0 && otherTjHit != UINT_MAX) {
        // get the trajectory index
        unsigned short otherTj = slc.slHits[otherTjHit].InTraj - 1;
        Trajectory& otj = slc.tjs[otherTj];
        // find out what point the hit is in
        unsigned short atPt = USHRT_MAX;
        for(unsigned short ipt = 0; ipt < otj.Pts.size(); ++ipt) {
          for(auto& iht : otj.Pts[ipt].Hits) {
            if(iht == otherTjHit) {
              atPt = ipt;
              break;
            } // iht == otherTjHit
          } // iht
          if(atPt != USHRT_MAX) break;
        } // ipt
        if(atPt != USHRT_MAX && DeltaAngle(tp.Ang, otj.Pts[atPt].Ang) < 0.1) {
          if(tcc.dbgStp) mf::LogVerbatim("TC")<<" Found a VLA merge candidate trajectory "<<otj.ID<<". Set StopFlag[kAtTj] and stop stepping";
          tj.StopFlag[1][kAtTj] = true;
          return;
        } // atPt is valid
      } // nAvailable == 0 &&
    } // stop at Tj
    
    for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) {
      unsigned int iht = tp.Hits[ii];
      if(slc.slHits[iht].InTraj != 0) continue;
      tp.UseHit[ii] = true;
      slc.slHits[iht].InTraj = tj.ID;
    } // ii
    DefineHitPos(slc, tp);
    SetEndPoints(tj);
    UpdateTjChgProperties("ALAH", slc, tj, tcc.dbgStp);
    
  } // AddLAHits

  //////////////////////////////////////////
  void ReversePropagate(TCSlice& slc, Trajectory& tj)
  {
    // Reverse the trajectory and step in the opposite direction. The
    // updated trajectory is returned if this process is successful
    
    if(!tcc.useAlg[kRvPrp]) return;
    
    if(tj.Pts.size() < 6) return;
    // only do this once
    if(tj.AlgMod[kRvPrp]) return;
    
    // This code can't handle VLA trajectories
    if(tj.Pts[tj.EndPt[0]].AngleCode == 2) return;
    
    bool prt = (tcc.dbgStp || tcc.dbgAlg[kRvPrp]);
    
    if(tj.EndPt[0] > 0) {
      tj.Pts.erase(tj.Pts.begin(), tj.Pts.begin() + tj.EndPt[0]);
      SetEndPoints(tj);
    }
    
    if(prt) mf::LogVerbatim("TC")<<"ReversePropagate: Prepping Tj "<<tj.ID<<" incoming StepDir "<<tj.StepDir;
    
    short stepDir = tj.StepDir;
    
    // find the wire on which EndPt resides
    unsigned int wire0 = std::nearbyint(tj.Pts[0].Pos[0]);
    unsigned int nextWire = wire0 - tj.StepDir;
    
    // check for dead wires
    geo::PlaneID planeID = DecodeCTP(tj.CTP);
    unsigned short ipl = planeID.Plane;
    while(nextWire > slc.firstWire[ipl] && nextWire < slc.lastWire[ipl]) {
      if(slc.wireHitRange[ipl][nextWire].first >= 0) break;
      nextWire -= tj.StepDir;
    }
    if(nextWire == slc.lastWire[ipl] - 1) return;
    // clone the first point
    TrajPoint tp = tj.Pts[0];
    // strip off the hits
    tp.Hits.clear(); tp.UseHit.reset();
    // move it to the next wire
    MoveTPToWire(tp, (float)nextWire);
    // find close unused hits near this position
    float maxDelta = 10 * tj.Pts[tj.EndPt[1]].DeltaRMS;
    if(!FindCloseHits(slc, tp, maxDelta, kUnusedHits)) return;
    if(prt) mf::LogVerbatim("TC")<<" nUnused hits "<<tp.Hits.size()<<" at Pos "<<PrintPos(slc, tp);
    if(tp.Hits.empty()) return;
    // There are hits on the next wire. Make a copy, reverse it and try
    // to extend it with StepAway
    if(prt) {
      mf::LogVerbatim myprt("TC");
      myprt<<" tp.Hits ";
      for(auto& iht : tp.Hits) myprt<<" "<<PrintHit(slc.slHits[iht])<<"_"<<slc.slHits[iht].InTraj;
    } // tcc.dbgStp
    //
    // Make a working copy of tj
    Trajectory tjWork = tj;
    // So the first shall be last and the last shall be first
    ReverseTraj(slc, tjWork);
    // Flag it to use special cuts in StepAway
    tjWork.AlgMod[kRvPrp] = true;
    // We are doing this probably because the trajectory is stopping.
    // Reduce the number of fitted points to a small number
    unsigned short lastPt = tjWork.Pts.size() - 1;
    if(lastPt < 4) return;
    // update the charge
    float chg = 0;
    float cnt = 0;
    for(unsigned short ii = 0; ii < 4; ++ii) {
      unsigned short ipt = lastPt - ii;
      if(tjWork.Pts[ipt].Chg == 0) continue;
      chg += tjWork.Pts[ipt].Chg;
      ++cnt;
    } // ii
    if(cnt == 0) return;
    if(cnt > 1) tjWork.Pts[lastPt].AveChg = chg / cnt;
    StepAway(slc, tjWork);
    if(!tj.IsGood) {
      if(prt) mf::LogVerbatim("TC")<<" ReversePropagate StepAway failed";
      return;
    }
    // check the new stopping point
    ChkStopEndPts(slc, tjWork, tcc.dbgStp);
    // restore the original direction
    if(tjWork.StepDir != stepDir) ReverseTraj(slc, tjWork);
    tj = tjWork;
    // re-check the ends
    ChkStop(slc, tj);
    if(prt) {
      mf::LogVerbatim("TC")<<" ReversePropagate success. Outgoing StepDir "<<tj.StepDir;
      if(tj.Pts.size() < 50) PrintTrajectory("RP", slc, tj, USHRT_MAX);
    }
    
  } // ReversePropagate
  
  
  //////////////////////////////////////////
  void UseUnusedHits(TCSlice& slc)
  {
    if(slc.tjs.size() == 0) return;
    if(!tcc.useAlg[kUUH]) return;
    
    // max change in position allowed after adding all unused hits in a multiplet 
    float maxPosSep2 = 0.25;
    
    std::vector<unsigned int> hitsInMultiplet;
    for(unsigned short itj = 0; itj < slc.tjs.size(); ++itj) {
      Trajectory& tj = slc.tjs[itj];
      if(tj.AlgMod[kKilled]) continue;
      // Find the max delta
      unsigned short firstPt = tj.EndPt[0];
      unsigned short lastPt = tj.EndPt[1];
      for(unsigned short ipt = firstPt; ipt <= lastPt; ++ipt) {
        TrajPoint& tp = tj.Pts[ipt];
        if(AngleRange(tp) == 0) continue;
        if(tp.Hits.empty()) continue;
        bool hitsAdded = false;
        for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) {
          if(!tp.UseHit[ii]) continue;
          unsigned int iht = tp.Hits[ii];
          GetHitMultiplet(slc, iht, hitsInMultiplet);
          if(hitsInMultiplet.size() > 1) {
            for(unsigned short jj = ii + 1; jj < tp.Hits.size(); ++jj) {
              if(!tp.UseHit[jj]) continue;
              if(std::find(hitsInMultiplet.begin(), hitsInMultiplet.end(), tp.Hits[jj]) != hitsInMultiplet.end()) {
                tp.UseHit[jj] = true;
                slc.slHits[tp.Hits[jj]].InTraj = tj.ID;
                hitsAdded = true;
              }
            } // jj
          }
        } // ii
        if(hitsAdded) {
          // save the hit position
          std::array<float, 2> oldHitPos = tp.HitPos;
          DefineHitPos(slc, tp);
          // keep it if 
          if(PosSep2(tj.Pts[ipt].HitPos, oldHitPos) < maxPosSep2) {
            tj.AlgMod[kUUH] = true;
          } else {
            UnsetUsedHits(slc, tj.Pts[ipt]);
          }
        }
      } // ipt
      if(tj.AlgMod[kUUH]) SetEndPoints(tj);
    } // itj
    
  } // UseUnusedHits

  
  ////////////////////////////////////////////////
  void GetHitMultiplet(TCSlice& slc, unsigned int theHit, std::vector<unsigned int>& hitsInMultiplet)
  {
    unsigned short localIndex;
    GetHitMultiplet(slc, theHit, hitsInMultiplet, localIndex);
  } // GetHitMultiplet
  
  ////////////////////////////////////////////////
  void GetHitMultiplet(TCSlice& slc, unsigned int theHit, std::vector<unsigned int>& hitsInMultiplet, unsigned short& localIndex)
  {
    hitsInMultiplet.clear();
    localIndex = 0;
    if(theHit > slc.slHits.size() - 1) return;
    if(slc.slHits[theHit].InTraj == SHRT_MAX) return;
    hitsInMultiplet.resize(1);
    hitsInMultiplet[0] = theHit;
    
    auto& hit = (*evt.allHits)[slc.slHits[theHit].allHitsIndex];
    unsigned int theWire = hit.WireID().Wire;
    unsigned short ipl = hit.WireID().Plane;
    float theTime = hit.PeakTime();
    float theRMS = hit.RMS();
    float narrowHitCut = 1.5 * evt.aveHitRMS[ipl];
    bool theHitIsNarrow = (theRMS < narrowHitCut);
    float maxPeak = hit.PeakAmplitude();
    unsigned short imTall = theHit;
    unsigned short nNarrow = 0;
    if(theHitIsNarrow) nNarrow = 1;
    // look for hits < theTime but within hitSep
    if(theHit > 0) {
      for(unsigned int iht = theHit - 1; iht != 0; --iht) {
        auto& hit = (*evt.allHits)[slc.slHits[iht].allHitsIndex];
        if(hit.WireID().Wire != theWire) break;
        if(hit.WireID().Plane != ipl) break;
        float hitSep = tcc.multHitSep * theRMS;
        float rms = hit.RMS();
        if(rms > theRMS) {
          hitSep = tcc.multHitSep * rms;
          theRMS = rms;
        }
        float dTick = std::abs(hit.PeakTime() - theTime);
        if(dTick > hitSep) break;
        hitsInMultiplet.push_back(iht);
        if(rms < narrowHitCut) ++nNarrow;
        float peakAmp = hit.PeakAmplitude();
        if(peakAmp > maxPeak) {
          maxPeak = peakAmp;
          imTall = iht;
        }
        theTime = hit.PeakTime();
        if(iht == 0) break;
      } // iht
    } // iht > 0
    localIndex = hitsInMultiplet.size() - 1;
    // reverse the order so that hitsInMuliplet will be
    // returned in increasing time order
    if(hitsInMultiplet.size() > 1) std::reverse(hitsInMultiplet.begin(), hitsInMultiplet.end());
    // look for hits > theTime but within hitSep
    theTime = hit.PeakTime();
    theRMS = hit.RMS();
    for(unsigned int iht = theHit + 1; iht < slc.slHits.size(); ++iht) {
      auto& hit = (*evt.allHits)[slc.slHits[iht].allHitsIndex];
      if(hit.WireID().Wire != theWire) break;
      if(hit.WireID().Plane != ipl) break;
      if(slc.slHits[iht].InTraj == SHRT_MAX) continue;
      float hitSep = tcc.multHitSep * theRMS;
      float rms = hit.RMS();
      if(rms > theRMS) {
        hitSep = tcc.multHitSep * rms;
        theRMS = rms;
      }
      float dTick = std::abs(hit.PeakTime() - theTime);
      if(dTick > hitSep) break;
      hitsInMultiplet.push_back(iht);
      if(rms < narrowHitCut) ++nNarrow;
      float peakAmp = hit.PeakAmplitude();
      if(peakAmp > maxPeak) {
        maxPeak = peakAmp;
        imTall = iht;
      }
      theTime = hit.PeakTime();
    } // iht
    if(hitsInMultiplet.size() == 1) return;
    
    if(hitsInMultiplet.size() > 16) {
      // Found > 16 hits in a multiplet which would be bad for UseHit. Truncate it
      hitsInMultiplet.resize(16);
      return;
    }
    
    // Don't make a multiplet that includes a tall narrow hit with short fat hits
    if(nNarrow == hitsInMultiplet.size()) return;
    if(nNarrow == 0) return;
    
    if(theHitIsNarrow && theHit == imTall) {
      // theHit is narrow and it is the highest amplitude hit in the multiplet. Ignore any
      // others that are short and fat
      auto tmp = hitsInMultiplet;
      tmp.resize(1);
      tmp[0] = theHit;
      hitsInMultiplet = tmp;
    } else {
      // theHit is not narrow and it is not the tallest. Ignore a single hit if it is
      // the tallest and narrow
      auto& hit = (*evt.allHits)[slc.slHits[imTall].allHitsIndex];
      if(hit.RMS() < narrowHitCut) {
        unsigned short killMe = 0;
        for(unsigned short ii = 0; ii < hitsInMultiplet.size(); ++ii) {
          if(hitsInMultiplet[ii] == imTall) {
            killMe = ii;
            break;
          }
        } // ii
        hitsInMultiplet.erase(hitsInMultiplet.begin() + killMe);
      } // slc.slHits[imTall].RMS < narrowHitCut
    } // narrow / tall test
    
  } // GetHitMultiplet


  //////////////////////////////////////////
  float HitTimeErr(TCSlice& slc, unsigned int iht)
  {
    if(iht > slc.slHits.size() - 1) return 0;
    auto& hit = (*evt.allHits)[slc.slHits[iht].allHitsIndex];
    return hit.RMS() * tcc.unitsPerTick * tcc.hitErrFac * hit.Multiplicity();
  } // HitTimeErr
  
  //////////////////////////////////////////
  float HitsTimeErr2(TCSlice& slc, const std::vector<unsigned int>& hitVec)
  {
    // Estimates the error^2 of the time using all hits in hitVec
    if(hitVec.empty()) return 0;
    float err = tcc.hitErrFac * HitsRMSTime(slc, hitVec, kUnusedHits);
    return err * err;
  } // HitsTimeErr2
  
  
  ////////////////////////////////////////////////
  void ChkStopEndPts(TCSlice& slc, Trajectory& tj, bool prt)
  {
    // Analyze the end of the Tj after crawling has stopped to see if any of the points
    // should be used
    // TODO: This function results in a small loss of efficiency and needs work. Perhaps by requir
    
    if(!tcc.useAlg[kChkStopEP]) return;
    if(tj.AlgMod[kJunkTj]) return;
    
    unsigned short endPt = tj.EndPt[1];
    // ignore VLA Tjs
    if(tj.Pts[endPt].AngleCode > 1) return;
    // don't get too carried away with this
    if(tj.Pts.size() - endPt > 10) return;
    
    // Get a list of hits a few wires beyond the last point on the Tj
    geo::PlaneID planeID = DecodeCTP(tj.CTP);
    unsigned short plane = planeID.Plane;
    
    // find the last point that has hits on it
    unsigned short lastPt = tj.Pts.size() - 1;
    for(lastPt = tj.Pts.size() - 1; lastPt >= tj.EndPt[1]; --lastPt) if(!tj.Pts[lastPt].Hits.empty()) break;
    auto& lastTP = tj.Pts[lastPt];
    
    if(tcc.dbgStp) {
      mf::LogVerbatim("TC")<<"CSEP: checking "<<tj.ID<<" endPt "<<endPt<<" Pts size "<<tj.Pts.size()<<" lastPt Pos "<<PrintPos(slc, lastTP.Pos);
    }
    
    // Check the charge and delta of the last point if there were many points fit
    if(lastTP.NTPsFit > 10 && lastTP.DeltaRMS > 0 && (lastTP.Delta / lastTP.DeltaRMS) > 3 && lastTP.ChgPull > 3) {
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<" Removing last TP with large Delta "<<lastTP.Delta<<" and large ChgPull "<<lastTP.ChgPull;
      UnsetUsedHits(slc, lastTP);
      tj.AlgMod[kChkStopEP] = true;
      SetEndPoints(tj);
      // check again
      auto& tp = tj.Pts[tj.EndPt[1]];
      if(tp.DeltaRMS > 0 && (tp.Delta / tp.DeltaRMS) > 3 && tp.ChgPull > 3) {
        UnsetUsedHits(slc, tp);
        SetEndPoints(tj);
      }
      return;
    }
    
    TrajPoint ltp;
    ltp.CTP = tj.CTP;
    ltp.Pos = tj.Pts[endPt].Pos;
    ltp.Dir = tj.Pts[endPt].Dir;
    double stepSize = std::abs(1/ltp.Dir[0]);
    std::array<int, 2> wireWindow;
    std::array<float, 2> timeWindow;
    std::vector<int> closeHits;
    bool isClean = true;
    for(unsigned short step = 0; step < 10; ++step) {
      for(unsigned short iwt = 0; iwt < 2; ++iwt) ltp.Pos[iwt] += ltp.Dir[iwt] * stepSize;
      int wire = std::nearbyint(ltp.Pos[0]);
      wireWindow[0] = wire;
      wireWindow[1] = wire;
      timeWindow[0] = ltp.Pos[1] - 5;
      timeWindow[1] = ltp.Pos[1] + 5;
      bool hitsNear;
      auto tmp = FindCloseHits(slc, wireWindow, timeWindow, plane, kAllHits, true, hitsNear);
      // add close hits that are not associated with this tj
      for(auto iht : tmp) if(slc.slHits[iht].InTraj != tj.ID) closeHits.push_back(iht);
      float nWiresPast = 0;
      // Check beyond the end of the trajectory to see if there are hits there
      if(ltp.Dir[0] > 0) {
        // stepping +
        nWiresPast = ltp.Pos[0] - lastTP.Pos[0];
      }  else {
        // stepping -
        nWiresPast = lastTP.Pos[0] - ltp.Pos[0];
      }
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<" Found "<<tmp.size()<<" hits near pos "<<PrintPos(slc, ltp.Pos)<<" nWiresPast "<<nWiresPast;
      if(nWiresPast > 0.5) {
        if(!tmp.empty()) isClean = false;
        if(nWiresPast > 1.5) break;
      } // nWiresPast > 0.5
    } // step
    
    // count the number of available hits
    unsigned short nAvailable = 0;
    for(auto iht : closeHits) if(slc.slHits[iht].InTraj == 0) ++nAvailable;
    
    if(tcc.dbgStp) {
      mf::LogVerbatim myprt("TC");
      myprt<<"closeHits";
      for(auto iht : closeHits) myprt<<" "<<PrintHit(slc.slHits[iht]);
      myprt<<" nAvailable "<<nAvailable;
      myprt<<" isClean "<<isClean;
    } // prt
    
    if(!isClean || nAvailable != closeHits.size()) return;
    
    unsigned short originalEndPt = tj.EndPt[1] + 1;
    // looks clean so use all the hits
    for(unsigned short ipt = originalEndPt; ipt <= lastPt; ++ipt) {
      auto& tp = tj.Pts[ipt];
      bool hitsAdded = false;
      for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) {
        // This shouldn't happen but check anyway
        if(slc.slHits[tp.Hits[ii]].InTraj != 0) continue;
        tp.UseHit[ii] = true;
        slc.slHits[tp.Hits[ii]].InTraj = tj.ID;
        hitsAdded = true;
      } // ii
      if(hitsAdded) DefineHitPos(slc, tp);
    } // ipt
    tj.AlgMod[kChkStopEP] = true;
    SetEndPoints(tj);
    // Re-fitting the end might be a good idea but it's probably not necessary. The
    // values of Delta should have already been filled
    
    // require a Bragg peak
    ChkStop(slc, tj);
    if(!tj.StopFlag[1][kBragg]) {
      // restore the original
      for(unsigned short ipt = originalEndPt; ipt <= lastPt; ++ipt) UnsetUsedHits(slc, tj.Pts[ipt]);
      SetEndPoints(tj);
    } // no Bragg Peak
    
    UpdateTjChgProperties("CSEP", slc, tj, prt);
    
  } // ChkStopEndPts
  
  //////////////////////////////////////////
  void DefineHitPos(TCSlice& slc, TrajPoint& tp)
  {
    // defines HitPos, HitPosErr2 and Chg for the used hits in the trajectory point
    
    tp.Chg = 0;
    if(tp.Hits.empty()) return;
    
    unsigned short nused = 0;
    unsigned int iht = 0;
    for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) {
      if(tp.UseHit[ii]) {
        iht = tp.Hits[ii];
        ++nused;
      }
    }
    if(nused == 0) return;
    
    // don't bother with rest of this if there is only one hit since it can
    // only reside on one wire
    if(nused == 1) {
      auto& hit = (*evt.allHits)[slc.slHits[iht].allHitsIndex];
      tp.Chg = hit.Integral();
      // Normalize to 1 WSE path length
      float pathInv = std::abs(tp.Dir[0]);
      if(pathInv < 0.05) pathInv = 0.05;
      tp.Chg *= pathInv;
      tp.HitPos[0] = hit.WireID().Wire;
      tp.HitPos[1] = hit.PeakTime() * tcc.unitsPerTick;
      float wireErr = tp.Dir[1] * 0.289;
      float timeErr = tp.Dir[0] * HitTimeErr(slc, iht);
      tp.HitPosErr2 = wireErr * wireErr + timeErr * timeErr;
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<"DefineHitPos: singlet "<<std::fixed<<std::setprecision(1)<<tp.HitPos[0]<<":"<<(int)(tp.HitPos[1]/tcc.unitsPerTick)<<" ticks. HitPosErr "<<sqrt(tp.HitPosErr2);
      return;
    } // nused == 1
    
    // multiple hits possibly on different wires
    std::vector<unsigned int> hitVec;
    tp.Chg = 0;
    std::array<float, 2> newpos;
    float chg;
    newpos[0] = 0;
    newpos[1] = 0;
    // Find the wire range for hits used in the TP
    unsigned int loWire = INT_MAX;
    unsigned int hiWire = 0;
    for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) {
      if(!tp.UseHit[ii]) continue;
      unsigned int iht = tp.Hits[ii];
      auto& hit = (*evt.allHits)[slc.slHits[iht].allHitsIndex];
      chg = hit.Integral();
      unsigned int wire = hit.WireID().Wire;
      if(wire < loWire) loWire = wire;
      if(wire > hiWire) hiWire = wire;
      newpos[0] += chg * wire;
      newpos[1] += chg * hit.PeakTime();
      tp.Chg += chg;
      hitVec.push_back(iht);
    } // ii
    
    if(tp.Chg == 0) return;
    
    tp.HitPos[0] = newpos[0] / tp.Chg;
    tp.HitPos[1] = newpos[1] * tcc.unitsPerTick / tp.Chg;
    // Normalize to 1 WSE path length
    float pathInv = std::abs(tp.Dir[0]);
    if(pathInv < 0.05) pathInv = 0.05;
    tp.Chg *= pathInv;
    // Error is the wire error (1/sqrt(12))^2 if all hits are on one wire.
    // Scale it by the wire range
    float dWire = 1 + hiWire - loWire;
    float wireErr = tp.Dir[1] * dWire * 0.289;
    float timeErr2 = tp.Dir[0] * tp.Dir[0] * HitsTimeErr2(slc, hitVec);
    tp.HitPosErr2 = wireErr * wireErr + timeErr2;
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<"DefineHitPos: multiplet "<<std::fixed<<std::setprecision(1)<<tp.HitPos[0]<<":"<<(int)(tp.HitPos[1]/tcc.unitsPerTick)<<" ticks. HitPosErr "<<sqrt(tp.HitPosErr2);
    
  } // DefineHitPos
  
  
  //////////////////////////////////////////
  void FindUseHits(TCSlice& slc, Trajectory& tj, unsigned short ipt, float maxDelta, bool useChg)
  {
    // Hits have been associated with trajectory point ipt but none are used. Here we will
    // decide which hits to use.
    
    if(ipt > tj.Pts.size() - 1) return;
    TrajPoint& tp = tj.Pts[ipt];
    
    if(tp.Hits.empty()) return;
    
    // Use all available hits on the last pass for the first few points when starting out
    if(ipt < 3 && tj.Pass == tcc.minPts.size() - 1) {
      for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) {
        unsigned int iht = tp.Hits[ii];
        if(slc.slHits[iht].InTraj > 0) continue;
        tp.UseHit[ii] = true;
        slc.slHits[iht].InTraj = tj.ID;
      }
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<"FUH: Using all hits on seed trajectory on the last pass";
      return;
    } // last pass for the first two points
    
    // don't check charge when starting out
    if(ipt < 5) useChg = false; 
    float chgPullCut = 1000;
    if(useChg) chgPullCut = tcc.chargeCuts[0];
    // open it up for RevProp, since we might be following a stopping track
    if(tj.AlgMod[kRvPrp]) chgPullCut *= 2;
    // BB April 19, 2018: open it up for low MCSMom tjs
    if(tj.MCSMom < 30) chgPullCut *= 2;
    
    float expectedHitsRMS = ExpectedHitsRMS(slc, tp);
    if(tcc.dbgStp) {
      mf::LogVerbatim("TC")<<"FUH:  maxDelta "<<maxDelta<<" useChg requested "<<useChg<<" Norm AveChg "<<(int)tp.AveChg<<" tj.ChgRMS "<<std::setprecision(2)<<tj.ChgRMS<<" chgPullCut "<<chgPullCut<<" TPHitsRMS "<<(int)TPHitsRMSTick(slc, tp, kUnusedHits)<<" ExpectedHitsRMS "<<(int)expectedHitsRMS<<" AngCode "<<tp.AngleCode;
    }
    
    // inverse of the path length for normalizing hit charge to 1 WSE unit
    float pathInv = std::abs(tp.Dir[0]);
    if(pathInv < 0.05) pathInv = 0.05;
    
    // Find the hit that has the smallest delta and the number of available hits
    tp.Delta = maxDelta;
    float delta;
    unsigned short imbest = USHRT_MAX;
    std::vector<float> deltas(tp.Hits.size(), 100);
    // keep track of the best delta - even if it is used
    float bestDelta = maxDelta;
    unsigned short nAvailable = 0;
    unsigned short firstAvailable = USHRT_MAX;
    unsigned short lastAvailable = USHRT_MAX;
    unsigned short firstUsed = USHRT_MAX;
    unsigned short imBadRecoHit = USHRT_MAX;
    for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) {
      tp.UseHit[ii] = false;
      unsigned int iht = tp.Hits[ii];
      delta = PointTrajDOCA(slc, iht, tp);
      if(delta < bestDelta) bestDelta = delta;
      if(slc.slHits[iht].InTraj > 0) {
        if(firstUsed == USHRT_MAX) firstUsed = ii;
        continue;
      }
      auto& hit = (*evt.allHits)[slc.slHits[iht].allHitsIndex];
      if(hit.GoodnessOfFit() < 0 || hit.GoodnessOfFit() > 100) imBadRecoHit = ii;
      if(firstAvailable == USHRT_MAX) firstAvailable = ii;
      lastAvailable = ii;
      ++nAvailable;
      if(tcc.dbgStp) {
        if(useChg) {
          if(tcc.dbgStp) mf::LogVerbatim("TC")<<" "<<ii<<"  "<<PrintHit(slc.slHits[iht])<<" delta "<<delta<<" Norm Chg "<<(int)(hit.Integral() * pathInv);
        } else {
          if(tcc.dbgStp) mf::LogVerbatim("TC")<<" "<<ii<<"  "<<PrintHit(slc.slHits[iht])<<" delta "<<delta;
        }
      } // tcc.dbgStp
      deltas[ii] = delta;
      if(delta < tp.Delta) {
        tp.Delta = delta;
        imbest = ii;
      }
    } // ii
    
    float chgWght = 0.5;
    
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<" firstAvailable "<<firstAvailable<<" lastAvailable "<<lastAvailable<<" firstUsed "<<firstUsed<<" imbest "<<imbest<<" single hit. tp.Delta "<<std::setprecision(2)<<tp.Delta<<" bestDelta "<<bestDelta<<" path length "<<1 / pathInv<<" imBadRecoHit "<<imBadRecoHit;
    if(imbest == USHRT_MAX || nAvailable == 0) return;
    unsigned int bestDeltaHit = tp.Hits[imbest];
    
    // Don't try to use a multiplet if a hit in the middle is in a different trajectory
    if(tp.Hits.size() > 2 && nAvailable > 1 && firstUsed != USHRT_MAX && firstAvailable < firstUsed && lastAvailable > firstUsed) {
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<" A hit in the middle of the multiplet is used. Use only the best hit";
      tp.UseHit[imbest] = true;
      slc.slHits[bestDeltaHit].InTraj = tj.ID;
      return;
    } // Used hit inside multiplet
    
    if(tp.AngleCode == 1) {
      // Get the hits that are in the same multiplet as bestDeltaHit
      std::vector<unsigned int> hitsInMultiplet;
      unsigned short localIndex;
      GetHitMultiplet(slc, bestDeltaHit, hitsInMultiplet, localIndex);
      if(tcc.dbgStp) {
        mf::LogVerbatim myprt("TC");
        myprt<<" bestDeltaHit "<<PrintHit(slc.slHits[bestDeltaHit]);
        myprt<<" in multiplet:";
        for(auto& iht : hitsInMultiplet) myprt<<" "<<PrintHit(slc.slHits[iht]);
      }
      // Consider the case where a bad reco hit might be better. It is probably wider and
      // has more charge
      if(imBadRecoHit != USHRT_MAX) {
        unsigned int iht = tp.Hits[imBadRecoHit];
        auto& hit = (*evt.allHits)[slc.slHits[iht].allHitsIndex];
        if(hit.RMS() > HitsRMSTick(slc, hitsInMultiplet, kUnusedHits)) {
          if(tcc.dbgStp) mf::LogVerbatim("TC")<<" Using imBadRecoHit "<<PrintHit(slc.slHits[iht]);
          tp.UseHit[imBadRecoHit] = true;
          slc.slHits[iht].InTraj = tj.ID;
          return;
        }
      } // bad reco hit
      // Use the hits in the multiplet instead
      for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) {
        unsigned int iht = tp.Hits[ii];
        if(slc.slHits[iht].InTraj > 0) continue;
        if(std::find(hitsInMultiplet.begin(), hitsInMultiplet.end(), iht) == hitsInMultiplet.end()) continue;
        tp.UseHit[ii] = true;
        slc.slHits[iht].InTraj = tj.ID;
      } // ii
      return;
    } // isLA
    
    // don't use the best UNUSED hit if the best delta is for a USED hit and it is much better
    // TY: ignore for RevProp
    if(bestDelta < 0.5 * tp.Delta && !tj.AlgMod[kRvPrp]) return;
    
    if(!useChg || (useChg && (tj.AveChg <= 0 || tj.ChgRMS <= 0))) {
      // necessary quantities aren't available for more careful checking
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<" tj.AveChg "<<tj.AveChg<<" or tj.ChgRMS "<<tj.ChgRMS<<". Use the best hit";
      tp.UseHit[imbest] = true;
      slc.slHits[bestDeltaHit].InTraj = tj.ID;
      return;
    }
    
    // Don't try to get fancy if we are tracking a long muon
    if(tj.PDGCode == 13 && bestDelta < 0.5) {
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<" Tracking muon. Use the best hit";
      tp.UseHit[imbest] = true;
      slc.slHits[bestDeltaHit].InTraj = tj.ID;
      return;
    }
    
    // The best hit is the only one available or this is a small angle trajectory
    if(nAvailable == 1 || tp.AngleCode == 0) {
      auto& hit = (*evt.allHits)[slc.slHits[bestDeltaHit].allHitsIndex];
      float bestDeltaHitChgPull = std::abs(hit.Integral() * pathInv / tp.AveChg - 1) / tj.ChgRMS;
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<" bestDeltaHitChgPull "<<bestDeltaHitChgPull<<" chgPullCut "<<chgPullCut;
      if(bestDeltaHitChgPull < chgPullCut || tp.Delta < 0.1) {
        tp.UseHit[imbest] = true;
        slc.slHits[bestDeltaHit].InTraj = tj.ID;
      } // good charge or very good delta
      return;
    } // bestDeltaHitMultiplicity == 1
    
    // Find the expected width for the angle of this TP (ticks)
    float expectedWidth = ExpectedHitsRMS(slc, tp);
    
    // Handle two available hits
    if(nAvailable == 2) {
      // See if these two are in the same multiplet and both are available
      std::vector<unsigned int> tHits;
      unsigned short localIndex;
      GetHitMultiplet(slc, bestDeltaHit, tHits, localIndex);
      // ombest is the index of the other hit in tp.Hits that is in the same multiplet as bestDeltaHit
      // if we find it
      unsigned short ombest = USHRT_MAX;
      unsigned int otherHit = INT_MAX;
      if(tHits.size() == 2) {
        otherHit = tHits[1 - localIndex];
        // get the index of this hit in tp.Hits
        for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) {
          if(slc.slHits[tp.Hits[ii]].InTraj > 0) continue;
          if(tp.Hits[ii] == otherHit) {
            ombest = ii;
            break;
          }
        } // ii
      } // tHits.size() == 2
      if(tcc.dbgStp) {
        mf::LogVerbatim("TC")<<" Doublet: imbest "<<imbest<<" ombest "<<ombest;
      }
      // The other hit exists in the tp and it is available
      if(ombest < tp.Hits.size()) {
        // compare the best delta hit and the other hit separately and the doublet as a merged pair
        float bestHitDeltaErr = std::abs(tp.Dir[1]) * 0.17 + std::abs(tp.Dir[0]) * HitTimeErr(slc, bestDeltaHit);
        // Construct a FOM starting with the delta pull
        float bestDeltaHitFOM = deltas[imbest] /  bestHitDeltaErr;
        if(bestDeltaHitFOM < 0.5) bestDeltaHitFOM = 0.5;
        // multiply by the charge pull if it is significant
        auto& hit = (*evt.allHits)[slc.slHits[bestDeltaHit].allHitsIndex];
        float bestDeltaHitChgPull = std::abs(hit.Integral() * pathInv / tp.AveChg - 1) / tj.ChgRMS;
        if(bestDeltaHitChgPull > 1) bestDeltaHitFOM *= chgWght * bestDeltaHitChgPull;
        // scale by the ratio
        float rmsRat = hit.RMS() / expectedWidth;
        if(rmsRat < 1) rmsRat = 1 / rmsRat;
        bestDeltaHitFOM *= rmsRat;
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<" bestDeltaHit FOM "<<deltas[imbest]/bestHitDeltaErr<<" bestDeltaHitChgPull "<<bestDeltaHitChgPull<<" rmsRat "<<rmsRat<<" bestDeltaHitFOM "<<bestDeltaHitFOM;
        // Now do the same for the other hit
        float otherHitDeltaErr = std::abs(tp.Dir[1]) * 0.17 + std::abs(tp.Dir[0]) * HitTimeErr(slc, otherHit);
        float otherHitFOM = deltas[ombest] /  otherHitDeltaErr;
        if(otherHitFOM < 0.5) otherHitFOM = 0.5;
        auto& ohit = (*evt.allHits)[slc.slHits[otherHit].allHitsIndex];
        float otherHitChgPull = std::abs(ohit.Integral() * pathInv / tp.AveChg - 1) / tj.ChgRMS;
        if(otherHitChgPull > 1) otherHitFOM *= chgWght * otherHitChgPull;
        rmsRat = ohit.RMS() / expectedWidth;
        if(rmsRat < 1) rmsRat = 1 / rmsRat;
        otherHitFOM *= rmsRat;
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<" otherHit FOM "<<deltas[ombest]/otherHitDeltaErr<<" otherHitChgPull "<<otherHitChgPull<<" rmsRat "<<rmsRat<<" otherHitFOM "<<otherHitFOM;
        // And for the doublet
        float doubletChg = hit.Integral() + ohit.Integral();
        float doubletTime = (hit.Integral() * hit.PeakTime() + ohit.Integral() * ohit.PeakTime()) / doubletChg;
        doubletChg *= pathInv;
        doubletTime *= tcc.unitsPerTick;
        float doubletWidthTick = TPHitsRMSTick(slc, tp, kUnusedHits);
        float doubletRMSTimeErr = doubletWidthTick * tcc.unitsPerTick;
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<" doublet Chg "<<doubletChg<<" doubletTime "<<doubletTime<<" doubletRMSTimeErr "<<doubletRMSTimeErr;
        float doubletFOM = PointTrajDOCA(slc, tp.Pos[0], doubletTime, tp) / doubletRMSTimeErr;
        if(doubletFOM < 0.5) doubletFOM = 0.5;
        float doubletChgPull = std::abs(doubletChg * pathInv / tp.AveChg - 1) / tj.ChgRMS;
        if(doubletChgPull > 1) doubletFOM *= chgWght * doubletChgPull;
        rmsRat = doubletWidthTick / expectedWidth;
        if(rmsRat < 1) rmsRat = 1 / rmsRat;
        doubletFOM *= rmsRat;
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<" doublet FOM "<<PointTrajDOCA(slc, tp.Pos[0], doubletTime, tp)/doubletRMSTimeErr<<" doubletChgPull "<<doubletChgPull<<" rmsRat "<<rmsRat<<" doubletFOM "<<doubletFOM;
        if(doubletFOM < bestDeltaHitFOM && doubletFOM < otherHitFOM) {
          tp.UseHit[imbest] = true;
          slc.slHits[bestDeltaHit].InTraj = tj.ID;
          tp.UseHit[ombest] = true;
          slc.slHits[otherHit].InTraj = tj.ID;
        } else {
          // the doublet is not the best
          if(bestDeltaHitFOM < otherHitFOM) {
            tp.UseHit[imbest] = true;
            slc.slHits[bestDeltaHit].InTraj = tj.ID;
          } else {
            tp.UseHit[ombest] = true;
            slc.slHits[otherHit].InTraj = tj.ID;
          } // otherHit is the best
        } // doublet is not the best
      } else {
        // the other hit isn't available. Just use the singlet
        tp.UseHit[imbest] = true;
        slc.slHits[bestDeltaHit].InTraj = tj.ID;
      }
      return;
    } // nAvailable == 2
    float hitsWidth = TPHitsRMSTick(slc, tp, kUnusedHits);
    float maxTick = tp.Pos[1] / tcc.unitsPerTick + 0.6 * expectedWidth;
    float minTick = tp.Pos[1] / tcc.unitsPerTick - 0.6 * expectedWidth;
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<" Multiplet: hitsWidth "<<hitsWidth<<" expectedWidth "<<expectedWidth<<" tick range "<<(int)minTick<<" "<<(int)maxTick;
    // use all of the hits in the tick window
    for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) {
      unsigned int iht = tp.Hits[ii];
      if(slc.slHits[iht].InTraj > 0) continue;
      auto& hit = (*evt.allHits)[slc.slHits[iht].allHitsIndex];
      if(hit.PeakTime() < minTick) continue;
      if(hit.PeakTime() > maxTick) continue;
      tp.UseHit[ii] = true;
      slc.slHits[iht].InTraj = tj.ID;
    }
    
  } // FindUseHits

  //////////////////////////////////////////
  void FindSoftKink(TCSlice& slc, Trajectory& tj)
  {
    // Looks for a soft kink in the trajectory and truncates it if one is found.
    // This is best done after FixTrajBegin has been called.
    
    if(!tcc.useAlg[kSoftKink]) return;
    if(tj.Pts.size() < 15) return;
    if(tj.MCSMom < 100) return;
    
    float dang = DeltaAngle(tj.Pts[tj.EndPt[0]].Ang, tj.Pts[tj.EndPt[1]].Ang);
    
    if(tcc.dbgStp) {
      mf::LogVerbatim("TC")<<"FindSoftKink: "<<tj.ID<<" dang "<<dang<<" cut "<<0.5 * tcc.kinkCuts[0];
    }
    if(dang < 0.5 * tcc.kinkCuts[0]) return;
    // require at least 5 points fitted at the end of the trajectory
    unsigned short endPt = tj.EndPt[1];
    if(tj.Pts[endPt].NTPsFit < 5) return;
    if(tj.Pts[endPt].NTPsFit > endPt) return;
    // Estimate where where the kink would be
    unsigned short kinkPt = endPt - tj.Pts[endPt].NTPsFit;
    // Require at least 5 points in the trajectory before the kink
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<" kinkPt "<<kinkPt<<" NTPsFit at kinkPt "<<tj.Pts[kinkPt].NTPsFit<<" max "<<0.5 * kinkPt;
    if(kinkPt < 5) return;
    // require fewer points fitted in this region compared the number of points prior to it
    if(tj.Pts[kinkPt].NTPsFit > 0.5 * kinkPt) return;
    // scan back until we find the maximum number of points fitted
    unsigned short maxPtsFit = tj.Pts[kinkPt].NTPsFit;
    unsigned short atPt = kinkPt;
    for(unsigned short ipt = kinkPt; kinkPt > tj.EndPt[0] + 5; --ipt) {
      if(tj.Pts[ipt].NTPsFit > maxPtsFit) {
        maxPtsFit = tj.Pts[ipt].NTPsFit;
        atPt = ipt;
      }
      // stop scanning when the max starts falling
      if(tj.Pts[ipt].NTPsFit < maxPtsFit) break;
      if(ipt == 0) break;
    } // ipt
    if(atPt < 5) return;
    // require the trajectory be straight before the kink - the section we are going to keep
    if(MCSMom(slc, tj, tj.EndPt[0], atPt) < 500) return;
    // release the hits in TPs after this point
    for(unsigned short ipt = atPt; ipt < tj.Pts.size(); ++ipt) UnsetUsedHits(slc, tj.Pts[ipt]);
    // Truncate the trajectory at this point
    tj.Pts.resize(atPt + 1);
    SetEndPoints(tj);
    tj.AlgMod[kSoftKink] = true;
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<" truncated trajectory at "<<PrintPos(slc, tj.Pts[tj.Pts.size()-1]);
    
  } // FindSoftKinks
  
  ////////////////////////////////////////////////
  void FillGaps(TCSlice& slc, Trajectory& tj)
  {
    // Fill in any gaps in the trajectory with close hits regardless of charge (well maybe not quite that)
    
    if(!tcc.useAlg[kFillGap]) return;
    if(tj.AlgMod[kJunkTj]) return;
    
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<"FG: Check Tj "<<tj.ID<<" from "<<PrintPos(slc, tj.Pts[tj.EndPt[0]])<<" to "<<PrintPos(slc, tj.Pts[tj.EndPt[1]]);
    
    // start with the first point that has charge
    short firstPtWithChg = tj.EndPt[0];
    bool first = true;
    float maxDelta = 1;
    // don't let MCSMom suffer too much while filling gaps
    short minMCSMom = 0.7 * tj.MCSMom;
    while(firstPtWithChg < tj.EndPt[1]) {
      short nextPtWithChg = firstPtWithChg + 1;
      // find the next point with charge
      for(nextPtWithChg = firstPtWithChg + 1; nextPtWithChg < tj.EndPt[1]; ++nextPtWithChg) {
        if(tj.Pts[nextPtWithChg].Chg > 0) break;
      } // nextPtWithChg
      if(nextPtWithChg == firstPtWithChg + 1) {
        // the next point has charge
        ++firstPtWithChg;
        continue;
      }
      // Found a gap. Require at least two consecutive points with charge after the gap
      if(nextPtWithChg < (tj.EndPt[1] - 1) && tj.Pts[nextPtWithChg + 1].Chg == 0) {
        firstPtWithChg = nextPtWithChg;
        continue;
      }
      // 10/1/2018 BB This shouldn't be a requirement
      // Compare the charge before and after
      if(!tcc.useAlg[kNewStpCuts] && tj.Pts[firstPtWithChg].Chg > 0) {
        float chgrat = tj.Pts[nextPtWithChg].Chg / tj.Pts[firstPtWithChg].Chg;
        if(chgrat < 0.7 || chgrat > 1.4) {
          firstPtWithChg = nextPtWithChg;
          continue;
        }
      }
      
      // Make a bare trajectory point at firstPtWithChg that points to nextPtWithChg
      TrajPoint tp;
      if(!MakeBareTrajPoint(slc, tj.Pts[firstPtWithChg], tj.Pts[nextPtWithChg], tp)) {
        tj.IsGood = false;
        return;
      }
      // Find the maximum delta between hits and the trajectory Pos for all
      // hits on this trajectory
      if(first) {
        maxDelta = 2.5 * MaxHitDelta(slc, tj);
        first = false;
      } // first
      // define a loose charge cut using the average charge at the first point with charge
      float maxChg = tj.Pts[firstPtWithChg].AveChg * (1 + 2 * tcc.chargeCuts[0] * tj.ChgRMS);
      // Eliminate the charge cut altogether if we are close to an end
      if(tj.Pts.size() < 10) {
        maxChg = 1E6;
      } else {
        short chgCutPt = tj.EndPt[0] + 5;
        if(firstPtWithChg < chgCutPt) {
          // gap is near end 0
          maxChg = 1E6;
        } else {
          // check for gap near end 1
          chgCutPt = tj.EndPt[1] - 5;
          if(chgCutPt < tj.EndPt[0]) chgCutPt = tj.EndPt[0];
          if(nextPtWithChg > chgCutPt) maxChg = 1E6;
        }
      }
      
      // fill in the gap
      for(unsigned short mpt = firstPtWithChg + 1; mpt < nextPtWithChg; ++mpt) {
        if(tj.Pts[mpt].Chg > 0) {
          mf::LogWarning("TC")<<"FillGaps coding error: firstPtWithChg "<<firstPtWithChg<<" mpt "<<mpt<<" nextPtWithChg "<<nextPtWithChg;
          slc.isValid = false;
          return;
        }
        bool filled = false;
        float chg = 0;
        for(unsigned short ii = 0; ii < tj.Pts[mpt].Hits.size(); ++ii) {
          unsigned int iht = tj.Pts[mpt].Hits[ii];
          if(slc.slHits[iht].InTraj > 0) continue;
          auto& hit = (*evt.allHits)[slc.slHits[iht].allHitsIndex];
          float delta = PointTrajDOCA(slc, iht, tp);
          if(tcc.dbgStp) mf::LogVerbatim("TC")<<" FG: "<<PrintPos(slc,tj.Pts[mpt])<<" hit "<<PrintHit(slc.slHits[iht])<<" delta "<<delta<<" maxDelta "<<maxDelta<<" Chg "<<hit.Integral()<<" maxChg "<<maxChg;
          if(delta > maxDelta) continue;
          tj.Pts[mpt].UseHit[ii] = true;
          slc.slHits[iht].InTraj = tj.ID;
          chg += hit.Integral();
          filled = true;
        } // ii
        if(chg > maxChg || MCSMom(slc, tj) < minMCSMom) {
          // don't use these hits after all
          UnsetUsedHits(slc, tj.Pts[mpt]);
          filled = false;
        }
        if(filled) {
          DefineHitPos(slc, tj.Pts[mpt]);
          tj.AlgMod[kFillGap] = true;
          if(tcc.dbgStp) {
            PrintTrajPoint("FG", slc, mpt, tj.StepDir, tj.Pass, tj.Pts[mpt]);
            mf::LogVerbatim("TC")<<"Check MCSMom "<<MCSMom(slc, tj);
          }
        } // filled
      } // mpt
      firstPtWithChg = nextPtWithChg;
    } // firstPtWithChg
    
    if(tj.AlgMod[kFillGap]) tj.MCSMom = MCSMom(slc, tj);
    
  } // FillGaps 
  
  ////////////////////////////////////////////////
  void CheckHiMultUnusedHits(TCSlice& slc, Trajectory& tj)
  {
    // Check for many unused hits in high multiplicity TPs in work and try to use them
    
    if(!tcc.useAlg[kCHMUH]) return;
    
    // This code might do bad things to short trajectories
    if(NumPtsWithCharge(slc, tj, true) < 6) return;
    if(tj.EndPt[0] == tj.EndPt[1]) return;
    // Angle code 0 tjs shouldn't have any high multiplicity hits added to them
    if(tj.Pts[tj.EndPt[1]].AngleCode == 0) return;
    
    // count the number of unused hits multiplicity > 1 hits and decide
    // if the unused hits should be used. This may trigger another
    // call to StepAway
    unsigned short ii, stopPt;
    // Use this to see if the high multiplicity Pts are mostly near
    // the end or further upstream
    unsigned short lastMult1Pt = USHRT_MAX;
    // the number of TPs with > 1 hit (HiMult)
    unsigned short nHiMultPt = 0;
    // the total number of hits associated with HiMult TPs
    unsigned short nHiMultPtHits = 0;
    // the total number of used hits associated with HiMult TPs
    unsigned short nHiMultPtUsedHits = 0;
    unsigned int iht;
    // start counting at the leading edge and break if a hit
    // is found that is used in a trajectory
    bool doBreak = false;
    unsigned short jj;
    for(ii = 1; ii < tj.Pts.size(); ++ii) {
      stopPt = tj.EndPt[1] - ii;
      for(jj = 0; jj < tj.Pts[stopPt].Hits.size(); ++jj) {
        iht = tj.Pts[stopPt].Hits[jj];
        if(slc.slHits[iht].InTraj > 0) {
          doBreak = true;
          break;
        }
      } // jj
      if(doBreak) break;
      // require 2 consecutive multiplicity = 1 points
      if(lastMult1Pt == USHRT_MAX && tj.Pts[stopPt].Hits.size() == 1 && tj.Pts[stopPt-1].Hits.size() == 1) lastMult1Pt = stopPt;
      if(tj.Pts[stopPt].Hits.size() > 1) {
        ++nHiMultPt;
        nHiMultPtHits += tj.Pts[stopPt].Hits.size();
        nHiMultPtUsedHits += NumHitsInTP(tj.Pts[stopPt], kUsedHits);
      } // high multiplicity TP
      // stop looking when two consecutive single multiplicity TPs are found
      if(lastMult1Pt != USHRT_MAX) break;
      if(stopPt == 1) break;
    } // ii
    // Don't do this if there aren't a lot of high multiplicity TPs
    float fracHiMult = (float)nHiMultPt / (float)ii;
    if(lastMult1Pt != USHRT_MAX) {
      float nchk = tj.EndPt[1] - lastMult1Pt + 1;
      fracHiMult = (float)nHiMultPt / nchk;
    } else {
      fracHiMult = (float)nHiMultPt / (float)ii;
    }
    float fracHitsUsed = 0;
    if(nHiMultPt > 0 && nHiMultPtHits > 0) fracHitsUsed = (float)nHiMultPtUsedHits / (float)nHiMultPtHits;
    // Use this to limit the number of points fit for trajectories that
    // are close the LA tracking cut
    ii = tj.EndPt[1];
    bool sortaLargeAngle = (AngleRange(tj.Pts[ii]) == 1);
    
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<"CHMUH: First InTraj stopPt "<<stopPt<<" fracHiMult "<<fracHiMult<<" fracHitsUsed "<<fracHitsUsed<<" lastMult1Pt "<<lastMult1Pt<<" sortaLargeAngle "<<sortaLargeAngle;
    if(fracHiMult < 0.3) return;
    if(fracHitsUsed > 0.98) return;
    
    float maxDelta = 2.5 * MaxHitDelta(slc, tj);
    
    if(tcc.dbgStp) {
      mf::LogVerbatim("TC")<<" Pts size "<<tj.Pts.size()<<" nHiMultPt "<<nHiMultPt<<" nHiMultPtHits "<<nHiMultPtHits<<" nHiMultPtUsedHits "<<nHiMultPtUsedHits<<" sortaLargeAngle "<<sortaLargeAngle<<" maxHitDelta "<<maxDelta;
    }
    
    // Use next pass cuts if available
    if(sortaLargeAngle && tj.Pass < tcc.minPtsFit.size()-1) ++tj.Pass;
    
    // Make a copy of tj in case something bad happens
    Trajectory TjCopy = tj;
    // and the list of used hits
    auto inTrajHits = PutTrajHitsInVector(tj, kUsedHits);
    unsigned short ipt;
    
    // unset the used hits from stopPt + 1 to the end
    for(ipt = stopPt + 1; ipt < tj.Pts.size(); ++ipt) UnsetUsedHits(slc, tj.Pts[ipt]);
    SetEndPoints(tj);
    unsigned short killPts;
    float delta;
    bool added;
    for(ipt = stopPt + 1; ipt < tj.Pts.size(); ++ipt) {
      // add hits that are within maxDelta and re-fit at each point
      added = false;
      for(ii = 0; ii < tj.Pts[ipt].Hits.size(); ++ii) {
        iht = tj.Pts[ipt].Hits[ii];
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<" ipt "<<ipt<<" hit "<<PrintHit(slc.slHits[iht])<<" inTraj "<<slc.slHits[iht].InTraj<<" delta "<<PointTrajDOCA(slc, iht, tj.Pts[ipt]);
        if(slc.slHits[iht].InTraj != 0) continue;
        delta = PointTrajDOCA(slc, iht, tj.Pts[ipt]);
        if(delta > maxDelta) continue;
        if (!NumHitsInTP(TjCopy.Pts[ipt], kUsedHits)||TjCopy.Pts[ipt].UseHit[ii]){
          tj.Pts[ipt].UseHit[ii] = true;
          slc.slHits[iht].InTraj = tj.ID;
          added = true;
        }
      } // ii
      if(added) DefineHitPos(slc, tj.Pts[ipt]);
      if(tj.Pts[ipt].Chg == 0) continue;
      tj.EndPt[1] = ipt;
      // This will be incremented by one in UpdateTraj
      if(sortaLargeAngle) tj.Pts[ipt].NTPsFit = 2;
      UpdateTraj(slc, tj);
      if(tj.NeedsUpdate) {
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<"UpdateTraj failed on point "<<ipt;
        // Clobber the used hits from the corrupted points in tj
        for(unsigned short jpt = stopPt + 1; jpt <= ipt; ++jpt) {
          for(unsigned short jj = 0; jj < tj.Pts[jpt].Hits.size(); ++jj) {
            if(tj.Pts[jpt].UseHit[jj]) slc.slHits[tj.Pts[jpt].Hits[jj]].InTraj = 0;
          } // jj
        } // jpt
        // restore the original trajectory
        tj = TjCopy;
        // restore the hits
        for(unsigned short jpt = stopPt + 1; jpt <= ipt; ++jpt) {
          for(unsigned short jj = 0; jj < tj.Pts[jpt].Hits.size(); ++jj) {
            if(tj.Pts[jpt].UseHit[jj]) slc.slHits[tj.Pts[jpt].Hits[jj]].InTraj = tj.ID;
          } // jj
        } // jpt
        return;
      }
      GottaKink(slc, tj, killPts);
      if(killPts > 0) {
        MaskTrajEndPoints(slc, tj, killPts);
        if(!tj.IsGood) return;
        break;
      }
      if(tcc.dbgStp) PrintTrajectory("CHMUH", slc, tj, ipt);
    } // ipt
    // if we made it here it must be OK
    SetEndPoints(tj);
    // Try to extend it, unless there was a kink
    if(tj.StopFlag[1][kAtKink]) return;
    // trim the end points although this shouldn't happen
    if(tj.EndPt[1] != tj.Pts.size() - 1) tj.Pts.resize(tj.EndPt[1] + 1);
    tj.AlgMod[kCHMUH] = true;
  } // CheckHiMultUnusedHits
  
  ////////////////////////////////////////////////
  void CheckHiMultEndHits(TCSlice& slc, Trajectory& tj)
  {
    // mask off high multiplicity TPs at the end
    if(!tcc.useAlg[kCHMEH]) return;
    if(tj.StopFlag[1][kBragg]) return;
    if(tj.Pts.size() < 10) return;
    if(tj.Pts[tj.EndPt[1]].AngleCode == 0) return;
    // find the average multiplicity in the first half
    unsigned short aveMult= 0;
    unsigned short ipt, nhalf = tj.Pts.size() / 2;
    unsigned short cnt = 0;
    for(auto& tp : tj.Pts) {
      if(tp.Chg == 0) continue;
      aveMult += tp.Hits.size();
      ++cnt;
      if(cnt == nhalf) break;
    } //  pt
    if(cnt == 0) return;
    aveMult /= cnt;
    if(aveMult == 0) aveMult = 1;
    // convert this into a cut
    aveMult *= 3;
    cnt = 0;
    for(ipt = tj.EndPt[1]; ipt > tj.EndPt[0]; --ipt) {
      if(tj.Pts[ipt].Chg == 0) continue;
      if(tj.Pts[ipt].Hits.size() > aveMult) {
        UnsetUsedHits(slc, tj.Pts[ipt]);
        ++cnt;
        continue;
      }
      break;
    } // ipt
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<"CHMEH multiplicity cut "<<aveMult<<" number of TPs masked off "<<cnt;
    if(cnt > 0) {
      tj.AlgMod[kCHMEH] = true;
      SetEndPoints(tj);
    }
  } // CheckHiMultEndHits
  
  ////////////////////////////////////////////////
  void HiEndDelta(TCSlice& slc, Trajectory& tj)
  {
    // Modify the trajectory at the end if there is a consistent increase in delta. It
    // is called from CheckTraj.
    // This needs to be done carefully...
    
    if(!tcc.useAlg[kHED]) return;
    if(tj.StopFlag[1][kBragg]) return;
    // Only consider long high momentum.
    if(tj.MCSMom < 100) return;
    if(tj.Pts.size() < 50) return;
    
    unsigned short ept = tj.EndPt[1];
    
    TrajPoint& lastTp = tj.Pts[ept];
    
    if(lastTp.AngleCode > 1) return;
    if(lastTp.FitChi < 1) return;
    
    unsigned short npts = USHRT_MAX;
    float lastDelta = lastTp.Delta;
    // check the last 20 points on the trajectory for a systematic increase in Delta and FitChi
    for(unsigned short ii = 1; ii < 20; ++ii) {
      unsigned short ipt = ept - ii;
      TrajPoint& tp = tj.Pts[ipt];
      if(tp.Chg == 0) continue;
      if(tp.FitChi < 1 || tp.Delta > lastDelta) {
        npts = ii;
        break;
      }
      lastDelta = tp.Delta;
    } // ii
    
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<"HED: last point FitChi "<<lastTp.FitChi<<" NTPsFit "<<lastTp.NTPsFit<<" new npts "<<npts;
    
    // something bad happened
    if(npts == USHRT_MAX) return;
    // The Tj end has some other problem
    if(npts < 4) return;
    
    // re-fit the end of the trajectory
    lastTp.NTPsFit = npts;
    FitTraj(slc, tj);
    if(tcc.dbgStp) PrintTrajPoint("HED", slc, ept, tj.StepDir, tj.Pass, lastTp);
    // update the last points
    for(unsigned short ii = 1; ii <= npts; ++ii) {
      unsigned short ipt = ept - ii;
      TrajPoint& tp = tj.Pts[ipt];
      if(tp.Chg == 0) continue;
      tp.Dir = tj.Pts[ept].Dir;
      tp.Ang = tj.Pts[ept].Ang;
      tp.AngErr = tj.Pts[ept].AngErr;
      tp.AngleCode = tj.Pts[ept].AngleCode;
      // Correct the projected time to the wire
      float dw = tp.Pos[0] - tj.Pts[ept].Pos[0];
      if(tp.Dir[0] != 0) tp.Pos[1] = tj.Pts[ept].Pos[1] + dw * tp.Dir[1] / tp.Dir[0];
      tp.Delta = PointTrajDOCA(slc, tp.HitPos[0], tp.HitPos[1], tp);
      tp.DeltaRMS = tj.Pts[ept].DeltaRMS;
      tp.NTPsFit = tj.Pts[ept].NTPsFit;
      tp.FitChi = tj.Pts[ept].FitChi;
      if(tcc.dbgStp) PrintTrajPoint("HED", slc, ipt, tj.StepDir, tj.Pass, tp);
    } // ii
    
    tj.AlgMod[kHED] = true;
    
  } // HiEndDelta

  //////////////////////////////////////////
  void UpdateDeltaRMS(TCSlice& slc, Trajectory& tj)
  {
    // Estimate the Delta RMS of the TPs on the end of tj.
    
    unsigned int lastPt = tj.EndPt[1];
    TrajPoint& lastTP = tj.Pts[lastPt];
    
    if(lastTP.Chg == 0) return;
    if(lastPt < 6) return;
    
    unsigned short ii, ipt, cnt = 0;
    float sum = 0;
    for(ii = 1; ii < tj.Pts.size(); ++ii) {
      ipt = lastPt - ii;
      if(ipt > tj.Pts.size() - 1) break;
      if(tj.Pts[ipt].Chg == 0) continue;
      sum += PointTrajDOCA(slc, tj.Pts[ipt].Pos[0], tj.Pts[ipt].Pos[1], lastTP);
      ++cnt;
      if(cnt == lastTP.NTPsFit) break;
      if(ipt == 0) break;
    }
    if(cnt < 3) return;
    // RMS of Gaussian distribution is ~1.2 x the average
    // of a one-sided Gaussian distribution (since Delta is > 0)
    lastTP.DeltaRMS = 1.2 * sum / (float)cnt;
    if(lastTP.DeltaRMS < 0.02) lastTP.DeltaRMS = 0.02;
    
  } // UpdateDeltaRMS
  
  //////////////////////////////////////////
  void MaskBadTPs(TCSlice& slc, Trajectory& tj, float const& maxChi)
  {
    // Remove TPs that have the worst values of delta until the fit chisq < maxChi
    
    if(!tcc.useAlg[kMaskBadTPs]) return;
    //don't use this function for reverse propagation
    if(!tcc.useAlg[kRvPrp]) return;
    
    bool prt = (tcc.dbgStp || tcc.dbgAlg[kMaskBadTPs]);
    
    if(tj.Pts.size() < 3) {
      //      mf::LogError("TC")<<"MaskBadTPs: Trajectory ID "<<tj.ID<<" too short to mask hits ";
      tj.IsGood = false;
      return;
    }
    unsigned short nit = 0;
    TrajPoint& lastTP = tj.Pts[tj.Pts.size() - 1];
    while(lastTP.FitChi > maxChi && nit < 3) {
      float maxDelta = 0;
      unsigned short imBad = USHRT_MAX;
      unsigned short cnt = 0;
      for(unsigned short ii = 1; ii < tj.Pts.size(); ++ii) {
        unsigned short ipt = tj.Pts.size() - 1 - ii;
        TrajPoint& tp = tj.Pts[ipt];
        if(tp.Chg == 0) continue;
        if(tp.Delta > maxDelta) {
          maxDelta = tp.Delta;
          imBad = ipt;
        }
        ++cnt;
        if(cnt == tp.NTPsFit) break;
      } // ii
      if(imBad == USHRT_MAX) return;
      if(prt) mf::LogVerbatim("TC")<<"MaskBadTPs: lastTP.FitChi "<<lastTP.FitChi<<"  Mask point "<<imBad;
      // mask the point
      UnsetUsedHits(slc, tj.Pts[imBad]);
      FitTraj(slc, tj);
      if(prt) mf::LogVerbatim("TC")<<"  after FitTraj "<<lastTP.FitChi;
      tj.AlgMod[kMaskBadTPs] = true;
      ++nit;
    } // lastTP.FItChi > maxChi && nit < 3
    
  } // MaskBadTPs

  ////////////////////////////////////////////////
  bool MaskedHitsOK(TCSlice& slc, Trajectory& tj)
  {
    // The hits in the TP at the end of the trajectory were masked off. Decide whether to continue stepping with the
    // current configuration (true) or whether to stop and possibly try with the next pass settings (false)
    
    if(!tcc.useAlg[kMaskHits]) return true;
    
    unsigned short lastPt = tj.Pts.size() - 1;
    if(tj.Pts[lastPt].Chg > 0) return true;
    unsigned short endPt = tj.EndPt[1];
    
    // count the number of points w/o used hits and the number with one unused hit
    unsigned short nMasked = 0;
    unsigned short nOneHit = 0;
    unsigned short nOKChg = 0;
    unsigned short nOKDelta = 0;
    // number of points with Pos > HitPos
    unsigned short nPosDelta = 0;
    // number of points with Delta increasing vs ipt
    unsigned short nDeltaIncreasing = 0;
    // Fake this a bit to simplify comparing the counts
    float prevDelta = tj.Pts[endPt].Delta;
    float maxOKDelta = 10 * tj.Pts[endPt].DeltaRMS;
    float maxOKChg = 0;
    // find the maximum charge point on the trajectory
    for(unsigned short ipt = tj.EndPt[0]; ipt <= tj.EndPt[1]; ++ipt) if(tj.Pts[ipt].Chg > maxOKChg) maxOKChg = tj.Pts[ipt].Chg;
    for(unsigned short ii = 1; ii < tj.Pts.size(); ++ii) {
      unsigned short ipt = tj.Pts.size() - ii;
      auto& tp = tj.Pts[ipt];
      if(tp.Chg > 0) break;
      unsigned short nUnusedHits = 0;
      float chg = 0;
      for(unsigned short jj = 0; jj < tp.Hits.size(); ++jj) {
        unsigned int iht = tp.Hits[jj];
        if(slc.slHits[iht].InTraj != 0) continue;
        ++nUnusedHits;
        auto& hit = (*evt.allHits)[slc.slHits[iht].allHitsIndex];
        chg += hit.Integral();
      } // jj
      if(chg < maxOKChg) ++nOKChg;
      if(nUnusedHits == 1) ++nOneHit;
      if(tp.Delta < maxOKDelta) ++nOKDelta;
      // count the number of points with Pos time > HitPos time
      if(tp.Pos[1] > tp.HitPos[1]) ++nPosDelta;
      // The number of increasing delta points: Note implied absolute value
      if(tp.Delta < prevDelta) ++nDeltaIncreasing;
      prevDelta = tp.Delta;
      ++nMasked;
    } // ii
    
    // determine if the hits are wandering away from the trajectory direction. This will result in
    // nPosDelta either being ~0 or ~equal to the number of masked points. nPosDelta should have something
    // in between these two extremes if we are stepping through a messy region
    bool driftingAway = nMasked > 2 && (nPosDelta == 0 || nPosDelta == nMasked);
    // Note that nDeltaIncreasing is always positive
    if(driftingAway && nDeltaIncreasing < nMasked - 1) driftingAway = false;
    
    if(tcc.dbgStp) {
      mf::LogVerbatim("TC")<<"MHOK:  nMasked "<<nMasked<<" nOneHit "<<nOneHit<<" nOKChg "<<nOKChg<<" nOKDelta "<<nOKDelta<<" nPosDelta "<<nPosDelta<<" nDeltaIncreasing "<<nDeltaIncreasing<<" driftingAway? "<<driftingAway;
    }
    
    if(!driftingAway) {
      if(nMasked < 8 || nOneHit < 8) return true;
      if(nOKDelta != nMasked) return true;
      if(nOKChg != nMasked) return true;
    }
    
    // we would like to reduce the number of fitted points to a minimum and include
    // the masked hits, but we can only do that if there are enough points
    if(tj.Pts[endPt].NTPsFit <= tcc.minPtsFit[tj.Pass]) {
      // stop stepping if we have masked off more points than are in the fit
      if(nMasked > tj.Pts[endPt].NTPsFit) return false;
      return true;
    }
    // Reduce the number of points fit and try to include the points
    unsigned short newNTPSFit;
    if(tj.Pts[endPt].NTPsFit > 2 * tcc.minPtsFit[tj.Pass]) {
      newNTPSFit = tj.Pts[endPt].NTPsFit / 2;
    } else {
      newNTPSFit = tcc.minPtsFit[tj.Pass];
    }
    for(unsigned ipt = endPt + 1; ipt < tj.Pts.size(); ++ipt) {
      TrajPoint& tp = tj.Pts[ipt];
      for(unsigned short ii = 0; ii < tj.Pts[ipt].Hits.size(); ++ii) {
        unsigned int iht = tp.Hits[ii];
        if(slc.slHits[iht].InTraj == 0) {
          tp.UseHit[ii] = true;
          slc.slHits[iht].InTraj = tj.ID;
          break;
        }
      } // ii
      DefineHitPos(slc, tp);
      SetEndPoints(tj);
      tp.NTPsFit = newNTPSFit;
      FitTraj(slc, tj);
      if(tcc.dbgStp) PrintTrajectory("MHOK", slc, tj, ipt);
    } // ipt
    
    tj.AlgMod[kMaskHits] = true;
    UpdateTjChgProperties("MHOK", slc, tj, tcc.dbgStp);
    return true;
    
  } // MaskedHitsOK

  ////////////////////////////////////////////////
  bool StopIfBadFits(TCSlice& slc, Trajectory& tj)
  {
    // Returns true if there are a number of Tps that were not used in the trajectory because the fit was poor and the
    // charge pull is not really high. This 
    
    // don't consider muons
    if(tj.PDGCode == 13) return false;
    // or long straight Tjs
    if(tj.Pts.size() > 40 && tj.MCSMom > 200) return false;
    
    unsigned short nBadFit = 0;
    unsigned short nHiChg = 0;
    unsigned short cnt = 0;
    for(unsigned short ipt = tj.Pts.size() - 1; ipt > tj.EndPt[1]; --ipt ) {
      if(tj.Pts[ipt].FitChi > 2) ++nBadFit;
      if(tj.Pts[ipt].ChgPull > 3) ++nHiChg;
      ++cnt;
      if(cnt == 5) break;
    } // ipt
    
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<"StopIfBadFits: nBadFit "<<nBadFit<<" nHiChg "<<nHiChg;
    if(nBadFit > 3 && nHiChg == 0) return true;
    return false;
    
  } // StopIfBadFits
  
  ////////////////////////////////////////////////
  void GottaKink(TCSlice& slc, Trajectory& tj, unsigned short& killPts)
  {
    // Checks the last few points on the trajectory and returns with the number of
    // points (killPts) that should be killed (aka masked) at the end
    // tcc.kinkCuts
    // 0 = kink angle cut (radians)
    // 1 = kink angle significance cut
    // 2 = nPts fit at the end of the tj
    // Kink angle cut = tcc.kinkCuts[0] + tcc.kinkCuts[1] * MCSThetaRMS
    
    killPts = 0;
    
    // decide whether to turn kink checking back on
    if(tcc.kinkCuts[0] > 0 && tj.EndPt[1] == 20) {
      if(MCSMom(slc, tj, 10, 19) > 50) tj.AlgMod[kNoKinkChk] = false;
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<"GottaKink turn kink checking back on? "<<tj.AlgMod[kNoKinkChk]<<" with MCSMom "<<MCSMom(slc, tj, 10, 19);
    }
    if(tj.AlgMod[kNoKinkChk]) return;
    
    unsigned short lastPt = tj.EndPt[1];
    if(lastPt < 5) return;
    if(tj.Pts[lastPt].Chg == 0) return;
    
    // MCSThetaRMS is the scattering angle for the entire length of the trajectory. Convert
    // this to the scattering angle for one WSE unit
    float thetaRMS = MCSThetaRMS(slc, tj, tj.EndPt[0], tj.EndPt[1]) / sqrt(TrajPointSeparation(tj.Pts[tj.EndPt[0]], tj.Pts[lastPt]));
    float kinkAngCut = tcc.kinkCuts[0] + tcc.kinkCuts[1] * thetaRMS;
    // relax this a bit when doing RevProp
    if(tj.AlgMod[kRvPrp]) kinkAngCut *= 1.3;
    
    // A simple check when there are few points being fit and the TJ is short.
    if(tj.Pts[lastPt].NTPsFit < 6 && tj.Pts.size() < 20) {
      unsigned short ii, prevPtWithHits = USHRT_MAX;
      unsigned short ipt;
      for(ii = 1; ii < tj.Pts.size(); ++ii) {
        ipt = lastPt - ii;
        if(tj.Pts[ipt].Chg > 0) {
          prevPtWithHits = ipt;
          break;
        }
        if(ipt == 0) break;
      } // ii
      if(prevPtWithHits == USHRT_MAX) return;
      float dang = DeltaAngle(tj.Pts[lastPt].Ang, tj.Pts[prevPtWithHits].Ang);
      kinkAngCut = 1.2 * tcc.kinkCuts[0];
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<"GottaKink Simple check lastPt "<<PrintPos(slc,tj.Pts[lastPt])<<" dang "<<dang<<" cut "<<kinkAngCut;
      if(dang > kinkAngCut) {
        killPts = 1;
        tj.StopFlag[1][kAtKink] = true;
      }
      // Another case where there are few hits fit just prior to a dead wire
      // section or there were no hits added for several steps or due to a large
      // value of tcc.maxWireSkipNoSignal. We just added a bogus hit just after this section
      // so the trajectory angle change will be small. Find the angle between the previous
      // point fitted angle and the angle formed by the last two TPs
      if(std::abs(tj.Pts[lastPt-1].Pos[0] - tj.Pts[lastPt].Pos[0]) > 3) {
        TrajPoint tmp;
        if(!MakeBareTrajPoint(slc, tj.Pts[lastPt-1], tj.Pts[lastPt], tmp)) {
          mf::LogVerbatim("TC")<<"GottaKink failure from MakeBareTrajPoint ";
          PrintTrajectory("GK", slc, tj, USHRT_MAX);
          tj.IsGood = false;
          return;
        }
        dang = DeltaAngle(tmp.Ang, tj.Pts[prevPtWithHits].Ang);
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<"GottaKink Simple check after gap lastPt "<<lastPt<<" prevPtWithHits "<<prevPtWithHits<<" dang "<<dang<<" cut "<<kinkAngCut;
        if(dang > 1.5 * kinkAngCut) {
          killPts = 1;
          tj.StopFlag[1][kAtKink] = true;
        }
      }
      return;
    } // tj.Pts[lastPt].NTPsFit < 6 && tj.Pts.size() < 20
    
    if(tj.EndPt[1] < 10) return;
    
    unsigned short kinkPt = USHRT_MAX;
    
    // Find the kinkPt which is tcc.kinkCuts[2] from the end that has charge
    unsigned short cnt = 0;
    unsigned short nPtsFit = tcc.kinkCuts[2];
    unsigned short nHiMultPt = 0;
    unsigned short nHiChg = 0;
    
    for(unsigned short ii = 1; ii < lastPt; ++ii) {
      unsigned short ipt = lastPt - ii;
      if(tj.Pts[ipt].Chg == 0) continue;
      ++cnt;
      if(tj.Pts[ipt].Hits.size() > 1) ++nHiMultPt;
      if(tj.Pts[ipt].ChgPull > 1.5) ++nHiChg;
      if(cnt == nPtsFit) {
        kinkPt = ipt;
        break;
      }
      if(ipt == 0) break;
    } // ii
    if(kinkPt == USHRT_MAX) return;
    
    TrajPoint tpFit;
    unsigned short npts = 4;
    unsigned short fitDir = -1;
    FitTraj(slc, tj, lastPt, npts, fitDir, tpFit);
    if(tpFit.FitChi > 1) return;
    
    float dang = DeltaAngle(tj.Pts[kinkPt].Ang, tpFit.Ang);
    
    if(dang > kinkAngCut) {
      killPts = nPtsFit;
      tj.StopFlag[1][kAtKink] = true;
    }
    
    if(killPts > 0) {
      // See if we are tracking a low momentum particle in which case we should just
      // turn off kink checking
      if(tcc.useAlg[kNoKinkChk] && tj.EndPt[1] < 20) {
        // Find MCSMom if it hasn't been done
        if(tj.MCSMom < 0) tj.MCSMom = MCSMom(slc, tj);
        if(tj.MCSMom < 50) {
          killPts = 0;
          tj.StopFlag[1][kAtKink] = false;
          tj.AlgMod[kNoKinkChk] = true;
          if(tcc.dbgStp) mf::LogVerbatim("TC")<<"GottaKink turning off kink checking. MCSMom "<<tj.MCSMom;
        }
      } // turn off kink check
      // Don't stop if the last few points had high charge pull and we are tracking a muon, but do mask off the hits
      if(killPts > 0 && tj.PDGCode == 13 && tj.Pts[lastPt].ChgPull > 2  && tj.Pts[lastPt-1].ChgPull > 2) tj.StopFlag[1][kAtKink] = false;
      // Don't keep stepping or mask off any TPs if we hit a kink while doing RevProp
      if(tj.AlgMod[kRvPrp]) killPts = 0;
      // see if this is a stopping tj
      ChkStop(slc, tj);
      if(tj.StopFlag[1][kBragg]) killPts = 0;
      // unset the stop bit
      tj.StopFlag[1][kBragg] = false;
    }
    
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<"GottaKink "<<kinkPt<<" Pos "<<PrintPos(slc, tj.Pts[kinkPt])<<" dang "<<std::fixed<<std::setprecision(2)<<dang<<" cut "<<kinkAngCut<<" tpFit chi "<<tpFit.FitChi<<" killPts "<<killPts<<" GottaKink? "<<tj.StopFlag[1][kAtKink]<<" MCSMom "<<tj.MCSMom<<" thetaRMS "<<thetaRMS;
    
  } // GottaKink

  ////////////////////////////////////////////////
  void FixTrajBegin(TCSlice& slc, Trajectory& tj)
  {
    // Update the parameters at the beginning of the trajectory. The first
    // points may not belong to this trajectory since they were added when there was
    // little information. This information may be updated later if ReversePropagate is used
    
    if(!tcc.useAlg[kFixBegin]) return;
    if(tj.AlgMod[kJunkTj]) return;
    
    // don't do anything if this tj has been modified by ReversePropagate
    if(tj.AlgMod[kRvPrp]) return;
    
    // don't bother with really short tjs
    if(tj.Pts.size() < 3) return;
    /*
     unsigned short lastPtToChk = 10;
     if(tcc.useAlg[kFTBRvProp]) lastPtToChk = tj.EndPt[1];
     */
    unsigned short atPt = tj.EndPt[1];
    unsigned short maxPtsFit = 0;
    for(unsigned short ipt = tj.EndPt[0]; ipt < tj.EndPt[1]; ++ipt) {
      if(tj.Pts[ipt].Chg == 0) continue;
      if(tj.Pts[ipt].NTPsFit >= maxPtsFit) {
        maxPtsFit = tj.Pts[ipt].NTPsFit;
        atPt = ipt;
        // no reason to continue if there are a good number of points fitted
        if(maxPtsFit > 20) break;
      }
    } // ipt
    // find the first point that is in this fit
    unsigned short firstPtFit = tj.EndPt[0];
    unsigned short cnt = 0;
    for(unsigned short ii = 1; ii < tj.Pts.size(); ++ii) {
      if(ii > atPt) break;
      unsigned short ipt = atPt - ii;
      if(tj.Pts[ipt].Chg == 0) continue;
      ++cnt;
      if(cnt == maxPtsFit) {
        firstPtFit = ipt;
        break;
      } // full count
    } // ii
    
    bool needsRevProp = firstPtFit > 3;
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<"FTB: firstPtFit "<<firstPtFit<<" atPt "<<atPt;
    
    if(!needsRevProp) {
      // check one wire on the other side of EndPt[0] to see if there are hits that are available which could
      // be picked up by reverse propagation
      TrajPoint tp = tj.Pts[0];
      tp.Hits.clear();
      tp.UseHit.reset();
      // Move the TP "backwards"
      double stepSize = tcc.VLAStepSize;
      if(tp.AngleCode < 2) stepSize = std::abs(1/tp.Dir[0]);
      tp.Pos[0] -= tp.Dir[0] * stepSize * tj.StepDir;
      tp.Pos[1] -= tp.Dir[1] * stepSize * tj.StepDir;
      if(tcc.useAlg[kNewStpCuts]) {
        // launch RevProp if this wire is dead
        unsigned int wire = std::nearbyint(tp.Pos[0]);
        unsigned short plane = DecodeCTP(tp.CTP).Plane;
        needsRevProp = (wire < slc.nWires[plane] && slc.wireHitRange[plane][wire].first == -1);
        if(tcc.dbgStp && needsRevProp) mf::LogVerbatim("TC")<<"FTB: Previous wire "<<wire<<" is dead. Call ReversePropagate";
      } // NewStpCuts
      if(!needsRevProp) {
        // check for hits on a not-dead wire
        float maxDelta = 3 * tp.DeltaRMS;
        if(FindCloseHits(slc, tp, maxDelta, kUnusedHits) && !tp.Hits.empty()) {
          needsRevProp = true;
          if(tcc.dbgStp) {
            mf::LogVerbatim("TC")<<"FTB: Close unused hits found near EndPt[0] "<<tp.Hits.size()<<". Call ReversePropagate";
            PrintTrajPoint("FTB", slc, 0, tj.StepDir, tj.Pass, tp);
          }
        }
      } // !needsRevProp
    } // !needsRevProp
    
    if(tcc.dbgStp) {
      mf::LogVerbatim("TC")<<"FTB: maxPtsFit "<<maxPtsFit<<" at point "<<atPt<<" firstPtFit "<<firstPtFit<<" Needs ReversePropagate? "<<needsRevProp;
    }
    
    if(tcc.useAlg[kFTBRvProp] && needsRevProp) {
      // lop off the points before firstPtFit and reverse propagate
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<"  clobber TPs "<<PrintPos(slc, tj.Pts[0])<<" to "<<PrintPos(slc, tj.Pts[atPt])<<". Call TrimEndPts then ReversePropagate ";
      for(unsigned short ipt = 0; ipt < firstPtFit; ++ipt) UnsetUsedHits(slc, tj.Pts[ipt]);
      SetEndPoints(tj);
      tj.AlgMod[kFTBRvProp] = true;
      // Check for quality and trim if necessary before reverse propagation
      TrimEndPts("RPi", slc, tj, tcc.qualityCuts, tcc.dbgStp);
      if(tj.AlgMod[kKilled]) {
        tj.IsGood = false;
        return;
      }
      ReversePropagate(slc, tj);
      ChkStopEndPts(slc, tj, tcc.dbgStp);
    }
    // Clean up the first points if no reverse propagation was done
    if(!tj.AlgMod[kRvPrp]) FixTrajBegin(slc, tj, atPt);
    
  } // FixTrajBegin
  
  ////////////////////////////////////////////////
  void FixTrajBegin(TCSlice& slc, Trajectory& tj, unsigned short atPt)
  {
    // Update the parameters at the beginning of the trajectory starting at point atPt
    
    if(!tcc.useAlg[kFixBegin]) return;
    // ignore short trajectories
    unsigned short npwc = NumPtsWithCharge(slc, tj, false);
    if(npwc < 6) return;
    // ignore somewhat longer trajectories that are curly
    if(npwc < 10 && tj.MCSMom < 100) return;
    // ignore shower-like trajectories
    if(tj.PDGCode == 11) return;
    // ignore junk trajectories
    if(tj.AlgMod[kJunkTj]) return;
    // ignore stopping trajectories
    if(tj.StopFlag[0][kBragg]) return;
    
    
    unsigned short firstPt = tj.EndPt[0];
    
    if(atPt == tj.EndPt[0]) return;
    
    // Default is to use DeltaRMS of the last point on the Tj
    float maxDelta = 4 * tj.Pts[tj.EndPt[1]].DeltaRMS;
    if(tcc.useAlg[kNewStpCuts]) {
      // 10/2/2018 BB Change requirement
      // Find the max DeltaRMS of points from atPt to EndPt[1]
      float maxDeltaRMS = 0;
      for(unsigned short ipt = atPt; ipt <= tj.EndPt[1]; ++ipt) {
        if(tj.Pts[ipt].DeltaRMS > maxDeltaRMS) maxDeltaRMS = tj.Pts[ipt].DeltaRMS;
      } // ipt
      maxDelta = 3 * maxDeltaRMS;
    } // kNewStpCuts
    
    if(tcc.dbgStp) {
      mf::LogVerbatim("TC")<<"FixTrajBegin: atPt "<<atPt<<" firstPt "<<firstPt<<" Stops at end 0? "<<PrintStopFlag(tj, 0)<<" start vertex "<<tj.VtxID[0]<<" maxDelta "<<maxDelta;
    }
    
    // update the trajectory for all the points up to atPt
    // assume that we will use all of these points
    bool maskedPts = false;
    for(unsigned short ii = 1; ii < tj.Pts.size(); ++ii) {
      if(ii > atPt) break;
      unsigned int ipt = atPt - ii;
      TrajPoint& tp = tj.Pts[ipt];
      tp.Dir = tj.Pts[atPt].Dir;
      tp.Ang = tj.Pts[atPt].Ang;
      tp.AngErr = tj.Pts[atPt].AngErr;
      tp.AngleCode = tj.Pts[atPt].AngleCode;
      // Correct the projected time to the wire
      float dw = tp.Pos[0] - tj.Pts[atPt].Pos[0];
      if(tp.Dir[0] != 0) tp.Pos[1] = tj.Pts[atPt].Pos[1] + dw * tp.Dir[1] / tp.Dir[0];
      tj.Pts[ipt].Delta = PointTrajDOCA(slc, tj.Pts[ipt].HitPos[0], tj.Pts[ipt].HitPos[1], tj.Pts[ipt]);
      tj.Pts[ipt].DeltaRMS = tj.Pts[atPt].DeltaRMS;
      tj.Pts[ipt].NTPsFit = tj.Pts[atPt].NTPsFit;
      tj.Pts[ipt].FitChi = tj.Pts[atPt].FitChi;
      tj.Pts[ipt].AveChg = tj.Pts[atPt].AveChg;
      tj.Pts[ipt].ChgPull = (tj.Pts[ipt].Chg / tj.AveChg - 1) / tj.ChgRMS;
      bool maskThisPt = (tj.Pts[ipt].Delta > maxDelta);
      if(maskThisPt) maskedPts = true;
      if(tcc.useAlg[kNewStpCuts]) {
        // 10/1/18 BB only mask off the bad point. Not all of them to the end
        if(maskThisPt) {
          UnsetUsedHits(slc, tp);
          if(tcc.dbgStp) mf::LogVerbatim("TC")<<" mask off "<<PrintPos(slc, tj.Pts[ipt].Pos)<<" "<<tj.Pts[ipt].Delta;
        } // maskThisPt
      } else {
        // old cuts - mask off all points to the beginning
        if(maskedPts) {
          UnsetUsedHits(slc, tp);
          if(tcc.dbgStp) mf::LogVerbatim("TC")<<" mask off "<<PrintPos(slc, tj.Pts[ipt].Pos)<<" "<<tj.Pts[ipt].Delta;
        } // maskedPts
      } // old cuts
      if(ipt == 0) break;
    } // ii
    if(maskedPts) SetEndPoints(tj);
    tj.AlgMod[kFixBegin] = true;
    
  } // FixTrajBegin
  
  ////////////////////////////////////////////////
  void FixTrajEnd(TCSlice& slc, Trajectory& tj, unsigned short atPt)
  {
    // Update the parameters at the end of the trajectory starting at point atPt
    
    if(!tcc.useAlg[kFixEnd]) return;
    // ignore short trajectories
    unsigned short npwc = NumPtsWithCharge(slc, tj, false);
    if(npwc < 6) return;
    // ignore somewhat longer trajectories that are curly
    if(npwc < 10 && tj.MCSMom < 100) return;
    // ignore shower-like trajectories
    if(tj.PDGCode == 11) return;
    // ignore junk trajectories
    if(tj.AlgMod[kJunkTj]) return;
    // ingore stopping trajectories
    if(tj.StopFlag[1][kBragg]) return;
    
    if(tcc.dbgStp) {
      mf::LogVerbatim("TC")<<"FixTrajEnd: atPt "<<atPt;
    }
    
    if(atPt == tj.EndPt[1]) return;
    
    // update the trajectory for all the intervening points
    for(unsigned short ipt = atPt + 1; ipt <= tj.EndPt[1]; ++ipt) {
      TrajPoint& tp = tj.Pts[ipt];
      tp.Dir = tj.Pts[atPt].Dir;
      tp.Ang = tj.Pts[atPt].Ang;
      tp.AngErr = tj.Pts[atPt].AngErr;
      tp.AngleCode = tj.Pts[atPt].AngleCode;
      // Correct the projected time to the wire
      float dw = tp.Pos[0] - tj.Pts[atPt].Pos[0];
      if(tp.Dir[0] != 0) tp.Pos[1] = tj.Pts[atPt].Pos[1] + dw * tp.Dir[1] / tp.Dir[0];
      tj.Pts[ipt].Delta = PointTrajDOCA(slc, tj.Pts[ipt].HitPos[0], tj.Pts[ipt].HitPos[1], tj.Pts[ipt]);
      tj.Pts[ipt].DeltaRMS = tj.Pts[atPt].DeltaRMS;
      tj.Pts[ipt].NTPsFit = tj.Pts[atPt].NTPsFit;
      tj.Pts[ipt].FitChi = tj.Pts[atPt].FitChi;
      tj.Pts[ipt].AveChg = tj.Pts[atPt].AveChg;
      tj.Pts[ipt].ChgPull = (tj.Pts[ipt].Chg / tj.AveChg - 1) / tj.ChgRMS;
      if(tcc.dbgStp) {
        PrintTrajectory("FTE", slc, tj, ipt);
      }
    } // ipt
    tj.AlgMod[kFixEnd] = true;
    
  } // FixTrajEnd
  
  ////////////////////////////////////////////////
  bool IsGhost(TCSlice& slc, Trajectory& tj)
  {
    // Sees if trajectory tj shares many hits with another trajectory and if so merges them.
    
    if(!tcc.useAlg[kUseGhostHits]) return false;
    // ensure that tj is not a saved trajectory
    if(tj.ID > 0) return true;
    // or an already killed trajectory
    if(tj.AlgMod[kKilled]) return true;
    if(tj.Pts.size() < 3) return false;
    
    // vectors of traj IDs, and the occurrence count
    std::vector<int> tID;
    std::vector<unsigned short> tCnt;
    
    unsigned short hitCnt = 0;
    unsigned short nAvailable = 0;
    for(unsigned short ipt = tj.EndPt[0]; ipt <= tj.EndPt[1]; ++ipt) {
      for(unsigned short ii = 0; ii < tj.Pts[ipt].Hits.size(); ++ii) {
        // ignore hits used by this trajectory
        if(tj.Pts[ipt].UseHit[ii]) {
          ++hitCnt;
          continue;
        }
        unsigned int iht = tj.Pts[ipt].Hits[ii];
        if(slc.slHits[iht].InTraj > 0 && (unsigned int)slc.slHits[iht].InTraj <= slc.tjs.size()) {
          int tjid = slc.slHits[iht].InTraj;
          unsigned short indx;
          for(indx = 0; indx < tID.size(); ++indx) if(tID[indx] == tjid) break;
          if(indx == tID.size()) {
            tID.push_back(tjid);
            tCnt.push_back(1);
          } else {
            ++tCnt[indx];
          }
        } else {
          ++nAvailable;
        }
      } // ii
    } // ipt
    
    // Call it a ghost if > 1/3 of the hits are used by another trajectory
    hitCnt /= 3;
    int oldTjID = INT_MAX;
    
    if(tcc.dbgStp) {
      mf::LogVerbatim myprt("TC");
      myprt<<"IsGhost tj hits size cut "<<hitCnt<<" tID_tCnt";
      for(unsigned short ii = 0; ii < tCnt.size(); ++ii) myprt<<" "<<tID[ii]<<"_"<<tCnt[ii];
      myprt<<"\nAvailable hits "<<nAvailable;
    } // prt
    
    for(unsigned short ii = 0; ii < tCnt.size(); ++ii) {
      if(tCnt[ii] > hitCnt) {
        oldTjID = tID[ii];
        hitCnt = tCnt[ii];
      }
    } // ii
    if(oldTjID == INT_MAX) return false;
    int oldTjIndex = oldTjID - 1;
    
    // See if this looks like a short delta-ray on a long muon
    Trajectory& oTj = slc.tjs[oldTjIndex];
    if(oTj.PDGCode == 13 && hitCnt < 0.1 * oTj.Pts.size()) return false;
    
    // See if there are gaps in this trajectory indicating that it is really a ghost and not
    // just a crossing trajectory 
    // find the range of wires spanned by oTj
    int wire0 = INT_MAX;
    int wire1 = 0;
    for(auto& otp : oTj.Pts) {
      int wire = std::nearbyint(otp.Pos[0]);
      if(wire < wire0) wire0 = wire;
      if(wire > wire1) wire1 = wire;
    } // tp
    
    int nwires = wire1 - wire0 + 1;
    std::vector<float> oTjPos1(nwires, -1);
    unsigned short nMissedWires = 0;
    for(unsigned short ipt = oTj.EndPt[0]; ipt <= oTj.EndPt[1]; ++ipt) {
      if(oTj.Pts[ipt].Chg == 0) continue;
      int wire = std::nearbyint(oTj.Pts[ipt].Pos[0]);
      int indx = wire - wire0;
      if(indx < 0 || indx > nwires - 1) continue;
      oTjPos1[indx] = oTj.Pts[ipt].Pos[1];
      ++nMissedWires;
    } // ipt
    // count the number of ghost TPs
    unsigned short ngh = 0;
    // and the number with Delta > 0 relative to oTj
    unsigned short nghPlus = 0;
    // keep track of the first point and last point appearance of oTj
    unsigned short firstPtInoTj = USHRT_MAX;
    unsigned short lastPtInoTj = 0;
    TrajPoint tp = tj.Pts[tj.EndPt[0]];
    for(unsigned short ipt = tj.EndPt[0]; ipt <= tj.EndPt[1]; ++ipt) {
      if(tj.Pts[ipt].Chg > 0) {
        tp = tj.Pts[ipt];
        continue;
      }
      int wire = std::nearbyint(tj.Pts[ipt].Pos[0]);
      int indx = wire - wire0;
      if(indx < 0 || indx > nwires - 1) continue;
      if(oTjPos1[indx] > 0) {
        // ensure that the hits in this tp are used in oTj
        bool HitInoTj = false;
        for(unsigned short ii = 0; ii < tj.Pts[ipt].Hits.size(); ++ii) {
          unsigned int iht = tj.Pts[ipt].Hits[ii];
          if(slc.slHits[iht].InTraj ==  oldTjID) HitInoTj = true;
        } // ii
        if(HitInoTj) {
          ++ngh;
          MoveTPToWire(tp, tj.Pts[ipt].Pos[0]);
          if(tp.Pos[1] > oTjPos1[indx]) ++nghPlus;
          if(firstPtInoTj == USHRT_MAX) firstPtInoTj = ipt;
          lastPtInoTj = ipt;
        }
      } // oTjHasChg[indx]
    } // ipt
    
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<" Number of missed wires in oTj gaps "<<nMissedWires<<" Number of ghost hits in these gaps "<<ngh<<" nghPlus "<<nghPlus<<" cut "<<0.2 * nMissedWires;
    
    if(ngh < 0.2 * nMissedWires) return false;
    if(firstPtInoTj > lastPtInoTj) return false;
    
    // require all of the tj TPs to be on either the + or - side of the oTj trajectory
    if(!(nghPlus > 0.8 * ngh || nghPlus < 0.2 * ngh) ) return false;
    
    if(tcc.dbgStp) mf::LogVerbatim("TC")<<" Trajectory is a ghost of "<<oldTjID<<" first point in oTj "<<firstPtInoTj<<" last point "<<lastPtInoTj;
    
    // unset all of the shared hits
    for(unsigned short ipt = firstPtInoTj; ipt <= lastPtInoTj; ++ipt) {
      if(tj.Pts[ipt].Chg == 0) continue;
      UnsetUsedHits(slc, tj.Pts[ipt]);
      if(tcc.dbgStp) PrintTrajectory("IG", slc, tj, ipt);
    }
    // see how many points are left at the end
    ngh = 0;
    for(unsigned short ipt = lastPtInoTj; ipt <= tj.Pts.size(); ++ipt) {
      if(tj.Pts[ipt].Chg > 0) ++ngh;
    } // ipt
    // clobber those too?
    if(ngh > 0 && ngh < tcc.minPts[tj.Pass]) {
      for(unsigned short ipt = lastPtInoTj; ipt <= tj.EndPt[1]; ++ipt) {
        if(tj.Pts[ipt].Chg > 0) UnsetUsedHits(slc, tj.Pts[ipt]);
      } // ipt
    }
    SetEndPoints(tj);
    tj.Pts.resize(tj.EndPt[1] + 1);
    slc.tjs[oldTjIndex].AlgMod[kUseGhostHits] = true;
    TrimEndPts("IG", slc, tj, tcc.qualityCuts, tcc.dbgStp);
    if(tj.AlgMod[kKilled]) {
      tj.IsGood = false;
      if(tcc.dbgStp)  mf::LogVerbatim("TC")<<" Failed quality cuts";
      return true;
    }
    tj.MCSMom = MCSMom(slc, tj);
    if(tcc.dbgStp)  mf::LogVerbatim("TC")<<" New tj size "<<tj.Pts.size();
    return true;
    
  } // IsGhost
  
  ////////////////////////////////////////////////
  bool IsGhost(TCSlice& slc, std::vector<unsigned int>& tHits)
  {
    // Called by FindJunkTraj to see if the passed hits are close to an existing
    // trajectory and if so, they will be used in that other trajectory
    
    if(!tcc.useAlg[kUseGhostHits]) return false;
    
    if(tHits.size() < 2) return false;
    
    bool prt = (tcc.dbgStp || tcc.dbgAlg[kUseGhostHits]);
    
    // find all nearby hits
    std::vector<unsigned int> hitsInMuliplet, nearbyHits;
    for(auto iht : tHits) {
      GetHitMultiplet(slc, iht, hitsInMuliplet);
      // prevent double counting
      for(auto mht : hitsInMuliplet) {
        if(std::find(nearbyHits.begin(), nearbyHits.end(), mht) == nearbyHits.end()) {
          nearbyHits.push_back(mht);
        }
      } // mht
    } // iht
    
    // vectors of traj IDs, and the occurrence count
    std::vector<unsigned int> tID, tCnt;
    for(auto iht : nearbyHits) {
      if(slc.slHits[iht].InTraj <= 0) continue;
      unsigned int tid = slc.slHits[iht].InTraj;
      unsigned short indx = 0;
      for(indx = 0; indx < tID.size(); ++indx) if(tID[indx] == tid) break;
      if(indx == tID.size()) {
        tID.push_back(tid);
        tCnt.push_back(1);
      }  else {
        ++tCnt[indx];
      }
    } // iht
    if(tCnt.empty()) return false;
    
    // Call it a ghost if > 50% of the hits are used by another trajectory
    unsigned short tCut = 0.5 * tHits.size();
    int tid = INT_MAX;
    
    if(prt) {
      mf::LogVerbatim myprt("TC");
      myprt<<"IsGhost tHits size "<<tHits.size()<<" cut fraction "<<tCut<<" tID_tCnt";
      for(unsigned short ii = 0; ii < tCnt.size(); ++ii) myprt<<" "<<tID[ii]<<"_"<<tCnt[ii];
    } // prt
    
    for(unsigned short ii = 0; ii < tCnt.size(); ++ii) {
      if(tCnt[ii] > tCut) {
        tid = tID[ii];
        break;
      }
    } // ii
    if(tid > (int)slc.tjs.size()) return false;
    
    if(prt) mf::LogVerbatim("TC")<<" is ghost of trajectory "<<tid;
    
    // Use all hits in tHits that are found in itj
    for(auto& tp : slc.tjs[tid - 1].Pts) {
      for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) {
        unsigned int iht = tp.Hits[ii];
        if(slc.slHits[iht].InTraj != 0) continue;
        for(unsigned short jj = 0; jj < tHits.size(); ++jj) {
          unsigned int tht = tHits[jj];
          if(tht != iht) continue;
          tp.UseHit[ii] = true;
          slc.slHits[iht].InTraj = tid;
          break;
        } // jj
      } // ii
    } // tp
    slc.tjs[tid - 1].AlgMod[kUseGhostHits] = true;
    return true;
    
  } // IsGhost

  ////////////////////////////////////////////////
  void EndMerge(TCSlice& slc, CTP_t inCTP, bool lastPass)
  {
    // Merges trajectories end-to-end or makes vertices. Does a more careful check on the last pass
    
    if(slc.tjs.size() < 2) return;
    if(!tcc.useAlg[kMerge]) return;
    
    bool prt = (tcc.dbgMrg && tcc.dbgSlc && inCTP == debug.CTP);
    if(prt) mf::LogVerbatim("TC")<<"inside EndMerge slice "<<slices.size()-1<<" inCTP "<<inCTP<<" nTjs "<<slc.tjs.size()<<" lastPass? "<<lastPass;
    
    // Ensure that all tjs are in the same order
    short tccStepDir = 1;
    if(!tcc.modes[kStepDir]) tccStepDir = -1;
    for(auto& tj : slc.tjs) {
      if(tj.AlgMod[kKilled]) continue;
      if(tj.CTP != inCTP) continue;
      if(tj.StepDir != tccStepDir && !tj.AlgMod[kSetDir]) ReverseTraj(slc, tj);
    } // tj
    
    unsigned short maxShortTjLen = tcc.vtx2DCuts[0];
    
    // temp vector for checking the fraction of hits near a merge point
    std::vector<int> tjlist(2);
    
    float minChgRMS = 0.5 * (tcc.chargeCuts[1] + tcc.chargeCuts[2]);
    
    // iterate whenever a merge occurs since tjs will change. This is not necessary
    // when a vertex is created however.
    bool iterate = true;
    while(iterate) {
      iterate = false;
      for(unsigned int it1 = 0; it1 < slc.tjs.size(); ++it1) {
        auto& tj1 = slc.tjs[it1];
        if(tj1.AlgMod[kKilled]) continue;
        if(tj1.CTP != inCTP) continue;
        for(unsigned short end1 = 0; end1 < 2; ++end1) {
          // no merge if there is a vertex at the end
          if(tj1.VtxID[end1] > 0) continue;
          // make a copy of tp1 so we can mess with it
          TrajPoint tp1 = tj1.Pts[tj1.EndPt[end1]];
          // do a local fit on the lastpass only using the last 3 points
          if(lastPass && tp1.NTPsFit > 3) {
            // make a local copy of the tj
            auto ttj = slc.tjs[it1];
            auto& lastTP = ttj.Pts[ttj.EndPt[end1]];
            // fit the last 3 points
            lastTP.NTPsFit = 3;
            FitTraj(slc, ttj);
            tp1 = ttj.Pts[ttj.EndPt[end1]];
          } // last pass
          bool isVLA = (tp1.AngleCode == 2);
          float bestFOM = 5;
          if(isVLA) bestFOM = 20;
          float bestDOCA;
          unsigned int imbest = INT_MAX;
          for(unsigned int it2 = 0; it2 < slc.tjs.size(); ++it2) {
            if(it1 == it2) continue;
            auto& tj2 = slc.tjs[it2];
            // check for consistent direction
            if(tj1.StepDir != tj2.StepDir) continue;
            if(tj2.AlgMod[kKilled]) continue;
            if(tj2.CTP != inCTP) continue;
            // BB April 19, 2018: check for large fraction of overlapping wires
            float olf = OverlapFraction(slc, tj1, tj2);
            if(olf > 0.25) continue;
            unsigned short end2 = 1 - end1;
            // check for a vertex at this end
            if(tj2.VtxID[end2] > 0) continue;
            TrajPoint& tp2 = tj2.Pts[tj2.EndPt[end2]];
            TrajPoint& tp2OtherEnd = tj2.Pts[tj2.EndPt[end1]];
            // ensure that the other end isn't closer
            if(std::abs(tp2OtherEnd.Pos[0] - tp1.Pos[0]) < std::abs(tp2.Pos[0] - tp1.Pos[0])) continue;
            // ensure that the order is correct
            if(tj1.StepDir > 0) {
              if(tp2.Pos[0] < tp1.Pos[0] - 2) continue;
            } else {
              if(tp2.Pos[0] > tp1.Pos[0] + 2) continue;
            }
            // ensure that there is a signal on most of the wires between these points
            if(!SignalBetween(slc, tp1, tp2, 0.8)) {
              continue;
            }
            // Find the distance of closest approach for small angle merging
            // Inflate the doca cut if we are bridging a block of dead wires
            float dang = DeltaAngle(tp1.Ang, tp2.Ang);
            float doca = 15;
            if(isVLA) {
              // compare the minimum separation between Large Angle trajectories using a generous cut
              unsigned short ipt1, ipt2;
              TrajTrajDOCA(slc, tj1, tj2, ipt1, ipt2, doca);
              //              if(prt) mf::LogVerbatim("TC")<<" isVLA check ipt1 "<<ipt1<<" ipt2 "<<ipt2<<" doca "<<doca;
            } else {
              // small angle
              doca = PointTrajDOCA(slc, tp1.Pos[0], tp1.Pos[1], tp2);
            }
            float fom = dang * doca;
            if(fom < bestFOM) {
              bestFOM = fom;
              bestDOCA = doca;
              imbest = it2;
            }
          } // it2
          // No merge/vertex candidates
          if(imbest == INT_MAX) continue;
          
          // Make angle adjustments to tp1.
          unsigned int it2 = imbest;
          auto& tj2 = slc.tjs[imbest];
          unsigned short end2 = 1 - end1;
          bool loMCSMom = (tj1.MCSMom + tj2.MCSMom) < 150;
          // Don't use the angle at the end Pt for high momentum long trajectories in case there is a little kink at the end
          if(tj1.Pts.size() > 50 && tj1.MCSMom > 100) {
            if(end1 == 0) {
              tp1.Ang = tj1.Pts[tj1.EndPt[0] + 2].Ang;
            } else {
              tp1.Ang = tj1.Pts[tj1.EndPt[1] - 2].Ang;
            }
          } else if(loMCSMom) {
            // Low momentum - calculate the angle using the two Pts at the end
            unsigned short pt1, pt2;
            if(end1 == 0) {
              pt1 = tj1.EndPt[0];
              pt2 = pt1 + 1;
            } else {
              pt2 = tj1.EndPt[1];
              pt1 = pt2 - 1;
            }
            TrajPoint tpdir;
            if(MakeBareTrajPoint(slc, tj1.Pts[pt1], tj1.Pts[pt2], tpdir)) tp1.Ang = tpdir.Ang;
          } // low MCSMom
          // Now do the same for tj2
          TrajPoint tp2 = tj2.Pts[tj2.EndPt[end2]];
          if(tj2.Pts.size() > 50 && tj2.MCSMom > 100) {
            if(end1 == 0) {
              tp2.Ang = tj2.Pts[tj2.EndPt[0] + 2].Ang;
            } else {
              tp2.Ang = tj2.Pts[tj2.EndPt[1] - 2].Ang;
            }
          } else if(loMCSMom) {
            // Low momentum - calculate the angle using the two Pts at the end
            unsigned short pt1, pt2;
            if(end2 == 0) {
              pt1 = tj2.EndPt[0];
              pt2 = pt1 + 1;
            } else {
              pt2 = tj2.EndPt[1];
              pt1 = pt2 - 1;
            }
            TrajPoint tpdir;
            if(MakeBareTrajPoint(slc, tj2.Pts[pt1], tj2.Pts[pt2], tpdir)) tp2.Ang = tpdir.Ang;
          } // low MCSMom
          
          if(!isVLA && !SignalBetween(slc, tp1, tp2, 0.99)) continue;
          
          // decide whether to merge or make a vertex
          float dang = DeltaAngle(tp1.Ang, tp2.Ang);
          float sep = PosSep(tp1.Pos, tp2.Pos);
          
          float dangCut;
          float docaCut;
          float chgPull = 0;
          if(tp1.AveChg > tp2.AveChg) {
            chgPull = (tp1.AveChg / tp2.AveChg - 1) / minChgRMS;
          } else {
            chgPull = (tp2.AveChg / tp1.AveChg - 1) / minChgRMS;
          }
          if(loMCSMom) {
            // increase dangCut dramatically for low MCSMom tjs
            dangCut = 1.0;
            // and the doca cut
            docaCut = 2;
          } else {
            // do a more careful calculation of the angle cut
            unsigned short e0 = tj1.EndPt[0];
            unsigned short e1 = tj1.EndPt[1];
            float tj1len = TrajPointSeparation(tj1.Pts[e0], tj1.Pts[e1]);
            float thetaRMS1 = MCSThetaRMS(slc, tj1);
            // calculate (thetaRMS / sqrt(length) )^2
            thetaRMS1 *= thetaRMS1 / tj1len;
            // and now tj2
            e0 = tj2.EndPt[0];
            e1 = tj2.EndPt[1];
            float tj2len = TrajPointSeparation(tj2.Pts[e0], tj2.Pts[e1]);
            float thetaRMS2 = MCSThetaRMS(slc, tj2);
            thetaRMS2 *= thetaRMS2 / tj2len;
            float dangErr = 0.5 * sqrt(thetaRMS1 + thetaRMS2);
            dangCut = tcc.kinkCuts[0] + tcc.kinkCuts[1] * dangErr;
            docaCut = 1;
            if(isVLA) docaCut = 15;
          }
          
          // open up the cuts on the last pass
          float chgFracCut = tcc.vtx2DCuts[8];
          float chgPullCut = tcc.chargeCuts[0];
          if(lastPass) {
            docaCut *= 2;
            chgFracCut *= 0.5;
            chgPullCut *= 1.5;
          }
          
          // check the merge cuts. Start with doca and dang requirements
          bool doMerge = bestDOCA < docaCut && dang < dangCut;
          bool showerTjs = tj1.PDGCode == 11 || tj2.PDGCode == 11;
          bool hiMCSMom = tj1.MCSMom > 200 || tj2.MCSMom > 200;
          // add a charge similarity requirement if not shower-like or low momentum or not LA
          if(doMerge && !showerTjs && hiMCSMom && chgPull > tcc.chargeCuts[0] && !isVLA) doMerge = false;
          // ignore the charge pull cut if both are high momentum and dang is really small
          if(!doMerge && tj1.MCSMom > 900 && tj2.MCSMom > 900 && dang < 0.1 && bestDOCA < docaCut) doMerge = true;
          
          // do not merge if chgPull is really high
          if(doMerge && chgPull > 2 * chgPullCut) doMerge = false;
          
          if(doMerge) {
            if(lastPass) {
              // last pass cuts are looser but ensure that the tj after merging meets the quality cut
              float npwc = NumPtsWithCharge(slc, tj1, true) + NumPtsWithCharge(slc, tj2, true);
              auto& tp1OtherEnd = tj1.Pts[tj1.EndPt[1 - end1]];
              auto& tp2OtherEnd = tj2.Pts[tj2.EndPt[1 - end2]];
              float nwires = std::abs(tp1OtherEnd.Pos[0] - tp2OtherEnd.Pos[0]);
              if(nwires == 0) nwires = 1;
              float hitFrac = npwc / nwires;
              doMerge = (hitFrac > tcc.qualityCuts[0]);
              //              if(prt) mf::LogVerbatim("TC")<<" lastPass merged Tj from "<<PrintPos(slc, tp1OtherEnd.Pos)<<" to "<<PrintPos(slc, tp2OtherEnd.Pos)<<" hitfrac "<<hitFrac<<" cut "<<tcc.qualityCuts[0]; 
            } else {
              // don't merge if the gap between them is longer than the length of the shortest Tj
              float len1 = TrajLength(slc.tjs[it1]);
              float len2 = TrajLength(slc.tjs[it2]);
              if(len1 < len2) {
                if(sep > len1) doMerge = false;
              } else {
                if(sep > len2) doMerge = false;
              }
              //              if(prt) mf::LogVerbatim("TC")<<" merge check sep "<<sep<<" len1 "<<len1<<" len2 "<<len2<<" Merge? "<<doMerge;
            } // not lastPass
          } // doMerge
          
          // Require a large charge fraction near a merge point
          tjlist[0] = slc.tjs[it1].ID;
          tjlist[1] = slc.tjs[it2].ID;
          float chgFrac = ChgFracNearPos(slc, tp1.Pos, tjlist);
          if(doMerge && bestDOCA > 1 && chgFrac < chgFracCut) doMerge = false;
          
          // don't merge if a Bragg peak exists. A vertex should be made instead
          if(doMerge && (tj1.StopFlag[end1][kBragg] || tj2.StopFlag[end2][kBragg])) doMerge = false;
          
          // Check the MCSMom asymmetry and don't merge if it is higher than the user-specified cut
          float momAsym = std::abs(tj1.MCSMom - tj2.MCSMom) / (float)(tj1.MCSMom + tj2.MCSMom);
          if(doMerge && momAsym > tcc.vtx2DCuts[9]) doMerge = false;
          
          // don't allow vertices to be created between delta-rays
          // This needs to be done more carefully
          //          if(!doMerge && (tj1.AlgMod[kDeltaRay] || tj2.AlgMod[kDeltaRay])) doMerge = true;
          
          if(prt) {
            mf::LogVerbatim myprt("TC");
            myprt<<"EM: T"<<slc.tjs[it1].ID<<"_"<<end1<<" - T"<<slc.tjs[it2].ID<<"_"<<end2<<" tp1-tp2 "<<PrintPos(slc, tp1)<<"-"<<PrintPos(slc, tp2);
            myprt<<" ShowerLike? "<<slc.tjs[it1].AlgMod[kShowerLike]<<" "<<slc.tjs[it2].AlgMod[kShowerLike];
            myprt<<" bestFOM "<<std::fixed<<std::setprecision(2)<<bestFOM;
            myprt<<" bestDOCA "<<std::setprecision(1)<<bestDOCA;
            myprt<<" cut "<<docaCut<<" isVLA? "<<isVLA;
            myprt<<" dang "<<std::setprecision(2)<<dang<<" dangCut "<<dangCut;
            myprt<<" chgPull "<<std::setprecision(1)<<chgPull<<" Cut "<<chgPullCut;
            myprt<<" chgFrac "<<std::setprecision(2)<<chgFrac;
            myprt<<" momAsym "<<momAsym;
            myprt<<" lastPass? "<<lastPass;
            myprt<<" doMerge? "<<doMerge;
          }
          
          if(bestDOCA > docaCut) continue;
          
          if(doMerge) {
            if(prt) mf::LogVerbatim("TC")<<"  Merge ";
            bool didMerge = false;
            if(end1 == 1) {
              didMerge = MergeAndStore(slc, it1, it2, tcc.dbgMrg);
            } else {
              didMerge = MergeAndStore(slc, it2, it1, tcc.dbgMrg);
            }
            if(didMerge) {
              // Set the end merge flag for the killed trajectories to aid tracing merges
              tj1.AlgMod[kMerge] = true;
              tj2.AlgMod[kMerge] = true;
              iterate = true;
            } // Merge and store successfull
            else {
              if(prt) mf::LogVerbatim("TC")<<"  MergeAndStore failed ";
            }
          } else {
            // create a vertex instead if it passes the vertex cuts
            VtxStore aVtx;
            aVtx.CTP = slc.tjs[it1].CTP;
            aVtx.ID = slc.vtxs.size() + 1;
            // keep it simple if tp1 and tp2 are very close or if the angle between them
            // is small
            if(PosSep(tp1.Pos, tp2.Pos) < 3 || dang < 0.1) {
              aVtx.Pos[0] = 0.5 * (tp1.Pos[0] + tp2.Pos[0]);
              aVtx.Pos[1] = 0.5 * (tp1.Pos[1] + tp2.Pos[1]);
              aVtx.Stat[kFixed] = true;
            } else {
              // Tps not so close
              // Dec 11, 2017. Require small separation in EndMerge.
              //              float sepCut = tcc.vtx2DCuts[2];
              float sepCut = tcc.vtx2DCuts[1];
              bool tj1Short = (slc.tjs[it1].EndPt[1] - slc.tjs[it1].EndPt[0] < maxShortTjLen);
              bool tj2Short = (slc.tjs[it2].EndPt[1] - slc.tjs[it2].EndPt[0] < maxShortTjLen);
              if(tj1Short || tj2Short) sepCut = tcc.vtx2DCuts[1];
              TrajIntersection(tp1, tp2, aVtx.Pos);
              float dw = aVtx.Pos[0] - tp1.Pos[0];
              if(std::abs(dw) > sepCut) continue;
              float dt = aVtx.Pos[1] - tp1.Pos[1];
              if(std::abs(dt) > sepCut) continue;
              dw = aVtx.Pos[0] - tp2.Pos[0];
              if(std::abs(dw) > sepCut) continue;
              dt = aVtx.Pos[1] - tp2.Pos[1];
              if(std::abs(dt) > sepCut) continue;
              // ensure that the vertex is not closer to the other end if the tj is short
              if(tj1Short) {
                TrajPoint otp1 = slc.tjs[it1].Pts[slc.tjs[it1].EndPt[1-end1]];
                if(PosSep2(otp1.Pos, aVtx.Pos) < PosSep2(tp1.Pos, aVtx.Pos)) continue;
              }
              if(tj2Short) {
                TrajPoint otp2 = slc.tjs[it2].Pts[slc.tjs[it2].EndPt[1-end2]];
                if(PosSep2(otp2.Pos, aVtx.Pos) < PosSep2(tp2.Pos, aVtx.Pos)) continue;
              }
              // we expect the vertex to be between tp1 and tp2
              if(aVtx.Pos[0] < tp1.Pos[0] && aVtx.Pos[0] < tp2.Pos[0]) {
                aVtx.Pos[0] = std::min(tp1.Pos[0], tp2.Pos[0]);
                aVtx.Stat[kFixed] = true;
              }
              if(aVtx.Pos[0] > tp1.Pos[0] && aVtx.Pos[0] > tp2.Pos[0]) {
                aVtx.Pos[0] = std::max(tp1.Pos[0], tp2.Pos[0]);
                aVtx.Stat[kFixed] = true;
              }
            } // Tps not so close
            // We got this far. Try a vertex fit to ensure that the errors are reasonable
            slc.tjs[it1].VtxID[end1] = aVtx.ID;
            slc.tjs[it2].VtxID[end2] = aVtx.ID;
            // save the position
            // do a fit
            if(!aVtx.Stat[kFixed] && !FitVertex(slc, aVtx, tcc.dbgMrg)) {
              // back out
              slc.tjs[it1].VtxID[end1] = 0;
              slc.tjs[it2].VtxID[end2] = 0;
              if(prt) mf::LogVerbatim("TC")<<" Vertex fit failed ";
              continue;
            }
            aVtx.NTraj = 2;
            aVtx.Pass = slc.tjs[it1].Pass;
            aVtx.Topo = end1 + end2;
            tj1.AlgMod[kMerge] = true;
            tj2.AlgMod[kMerge] = true;
            // Set pion-like PDGCodes
            if(tj1.StopFlag[end1][kBragg] && !tj2.StopFlag[end2][kBragg]) tj1.PDGCode = 211;
            if(tj2.StopFlag[end2][kBragg] && !tj1.StopFlag[end1][kBragg]) tj2.PDGCode = 211;
            if(!StoreVertex(slc, aVtx)) continue;
            SetVx2Score(slc);
            if(prt) {
              auto& newVx = slc.vtxs[slc.vtxs.size() - 1];
              mf::LogVerbatim("TC")<<"  New 2V"<<newVx.ID<<" at "<<(int)newVx.Pos[0]<<":"<<(int)(newVx.Pos[1]/tcc.unitsPerTick)<<" Score "<<newVx.Score;
            }
            // check the score and kill it if it is below the cut
            auto& newVx2 = slc.vtxs[slc.vtxs.size() - 1];
            if(newVx2.Score < tcc.vtx2DCuts[7] && CompatibleMerge(slc, tj1, tj2, tcc.dbgMrg)) {
              slc.tjs[it1].VtxID[end1] = 0;
              slc.tjs[it2].VtxID[end2] = 0;
              slc.vtxs.pop_back();
              bool didMerge = false;
              if(end1 == 1) {
                didMerge = MergeAndStore(slc, it1, it2, tcc.dbgMrg);
              } else {
                didMerge = MergeAndStore(slc, it2, it1, tcc.dbgMrg);
              }
              if(didMerge) {
                // Set the end merge flag for the killed trajectories to aid tracing merges
                tj1.AlgMod[kMerge] = true;
                tj1.AlgMod[kMerge] = true;
                iterate = true;
              } // Merge and store successfull
              else {
                if(prt) mf::LogVerbatim("TC")<<"  MergeAndStore failed ";
              }
            }
          } // create a vertex
          if(tj1.AlgMod[kKilled]) break;
        } // end1
      } // it1
    } // iterate
    
    ChkVxTjs(slc, inCTP, tcc.dbgMrg);
    /*
     // Do some checking in debug mode
     if(tcc.modes[kDebug] && lastPass) {
     for(unsigned short it1 = 0; it1 < slc.tjs.size() - 1; ++it1) {
     auto& tj1 = slc.tjs[it1];
     if(tj1.CTP != inCTP) continue;
     if(tj1.AlgMod[kKilled]) continue;
     for(unsigned short end1 = 0; end1 < 2; ++end1) {
     unsigned short end2 = 1 - end1;
     auto& tp1 = tj1.Pts[tj1.EndPt[end1]];
     for(unsigned short it2 = it1 + 1; it2 < slc.tjs.size(); ++it2) {
     auto& tj2 = slc.tjs[it2];
     if(tj2.CTP != inCTP) continue;
     if(tj2.AlgMod[kKilled]) continue;
     auto& tp2 = tj2.Pts[tj2.EndPt[end2]];
     float sep = PosSep2(tp1.HitPos, tp2.HitPos);
     if(sep < 2.5) {
     if(tj1.VtxID[end1] == 0 && tj2.VtxID[end2] == 0) {
     std::cout<<"Tjs "<<tj1.ID<<" and "<<tj2.ID<<" are close at Pos "<<tj1.CTP<<":"<<PrintPos(slc, tp1.HitPos)<<" "<<tj2.CTP<<":"<<PrintPos(slc, tp2.HitPos)<<" with no merge or vertex\n";
     } else if(tj1.VtxID[end1] != tj2.VtxID[end2]) {
     std::cout<<"Tjs "<<tj1.ID<<" and "<<tj2.ID<<" are close at Pos "<<tj1.CTP<<":"<<PrintPos(slc, tp1.HitPos);
     std::cout<<" but have different vertex IDs "<<tj1.VtxID[end1]<<" != "<<tj2.VtxID[end2];
     std::cout<<"\n";
     }
     } // close points
     } // it2
     } // end1
     } // it1
     } // debug mode
     */
  } // EndMerge
  
  //////////////////////////////////////////
  void MaskTrajEndPoints(TCSlice& slc, Trajectory& tj, unsigned short nPts)
  {
    
    // Masks off (sets all hits not-Used) nPts trajectory points at the leading edge of the
    // trajectory, presumably because the fit including this points is poor. The position, direction
    // and Delta of the last nPts points is updated as well
    
    if(tj.Pts.size() < 3) {
      mf::LogError("TC")<<"MaskTrajEndPoints: Trajectory ID "<<tj.ID<<" too short to mask hits ";
      tj.IsGood = false;
      return;
    }
    if(nPts > tj.Pts.size() - 2) {
      mf::LogError("TC")<<"MaskTrajEndPoints: Trying to mask too many points "<<nPts<<" Pts.size "<<tj.Pts.size();
      tj.IsGood = false;
      return;
    }
    
    // find the last good point (with charge)
    unsigned short lastGoodPt = USHRT_MAX ;
    
    if (!ChkMichel(slc, tj, lastGoodPt)){ //did not find michel electron
      for(unsigned short ii = 0; ii < tj.Pts.size(); ++ii) {
        unsigned short ipt = tj.EndPt[1] - nPts - ii;
        if(tj.Pts[ipt].Chg > 0) {
          lastGoodPt = ipt;
          break;
        }
        if(ipt == 0) break;
      } // ii
    }
    if(tcc.dbgStp) {
      mf::LogVerbatim("TC")<<"MTEP: lastGoodPt "<<lastGoodPt<<" Pts size "<<tj.Pts.size()<<" tj.IsGood "<<tj.IsGood;
    }
    if(lastGoodPt == USHRT_MAX) return;
    tj.EndPt[1] = lastGoodPt;
    
    //for(unsigned short ii = 0; ii < nPts; ++ii) {
    for(unsigned short ii = 0; ii < tj.Pts.size(); ++ii) {
      unsigned short ipt = tj.Pts.size() - 1 - ii;
      if (ipt==lastGoodPt) break;
      UnsetUsedHits(slc, tj.Pts[ipt]);
      // Reset the position and direction of the masked off points
      tj.Pts[ipt].Dir = tj.Pts[lastGoodPt].Dir;
      if(tj.Pts[lastGoodPt].AngleCode == 2) {
        // Very large angle: Move by path length
        float path = TrajPointSeparation(tj.Pts[lastGoodPt], tj.Pts[ipt]);
        tj.Pts[ipt].Pos[0] = tj.Pts[lastGoodPt].Pos[0] + path * tj.Pts[ipt].Dir[0];
        tj.Pts[ipt].Pos[1] = tj.Pts[lastGoodPt].Pos[1] + path * tj.Pts[ipt].Dir[1];
      } else {
        // Not large angle: Move by wire
        float dw = tj.Pts[ipt].Pos[0] - tj.Pts[lastGoodPt].Pos[0];
        // Correct the projected time to the wire
        float newpos = tj.Pts[lastGoodPt].Pos[1] + dw * tj.Pts[ipt].Dir[1] / tj.Pts[ipt].Dir[0];
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<"MTEP: ipt "<<ipt<<" Pos[0] "<<tj.Pts[ipt].Pos[0]<<". Move Pos[1] from "<<tj.Pts[ipt].Pos[1]<<" to "<<newpos;
        tj.Pts[ipt].Pos[1] = tj.Pts[lastGoodPt].Pos[1] + dw * tj.Pts[ipt].Dir[1] / tj.Pts[ipt].Dir[0];
      }
      tj.Pts[ipt].Delta = PointTrajDOCA(slc, tj.Pts[ipt].HitPos[0], tj.Pts[ipt].HitPos[1], tj.Pts[ipt]);
      if(tcc.dbgStp) mf::LogVerbatim("TC")<<" masked ipt "<<ipt<<" Pos "<<PrintPos(slc, tj.Pts[ipt])<<" Chg "<<tj.Pts[ipt].Chg;
    } // ii
    SetEndPoints(tj);
    
  } // MaskTrajEndPoints
  
  ////////////////////////////////////////////////
  void ChkStop(TCSlice& slc, Trajectory& tj)
  {
    // Sets the StopFlag[kBragg] bits on the trajectory by identifying the Bragg peak
    // at each end. This function checks both ends, finding the point with the highest charge nearest the
    // end and considering the first (when end = 0) 4 points or last 4 points (when end = 1). The next
    // 5 - 10 points (fChkStop[0]) are fitted to a line, Q(x - x0) = Qo + (x - x0) * slope where x0 is the
    // wire position of the highest charge point. A large negative slope indicates that there is a Bragg
    // peak at the end.
    
    tj.StopFlag[0][kBragg] = false;
    tj.StopFlag[1][kBragg] = false;
    if(!tcc.useAlg[kChkStop]) return;
    if(tcc.chkStopCuts[0] < 0) return;
    
    // don't attempt with low momentum trajectories
    if(tj.MCSMom < 30) return;
    
    // ignore trajectories that are very large angle at both ends
    if(tj.Pts[tj.EndPt[0]].AngleCode == 2 || tj.Pts[tj.EndPt[1]].AngleCode == 2) return;
    
    unsigned short nPtsToCheck = tcc.chkStopCuts[1];
    if(tj.Pts.size() < nPtsToCheck) return;
    
    bool prt = (tcc.dbgStp || tcc.dbgAlg[kChkStop]);
    
    if(prt) mf::LogVerbatim("TC")<<"ChkStop: requiring "<<nPtsToCheck<<" points with charge slope > "<<tcc.chkStopCuts[0]<<" Chg/WSEU";
    
    // find the highest charge hit in the first 3 points at each end
    for(unsigned short end = 0; end < 2; ++end) {
      short dir = 1 - 2 * end;
      // find the point with the highest charge considering the first 3 points
      float big = 0;
      unsigned short hiPt = 0;
      float wire0 = 0;
      for(unsigned short ii = 0; ii < 4; ++ii) {
        short ipt = tj.EndPt[end] + ii * dir;
        if(ipt < tj.EndPt[0] || ipt > tj.EndPt[1]) break;
        TrajPoint& tp = tj.Pts[ipt];
        if(tp.Chg > big) {
          big = tp.Chg;
          wire0 = tp.Pos[0];
          hiPt = ipt;
        }
      } // ii
      if(prt) mf::LogVerbatim("TC")<<" end "<<end<<" wire0 "<<wire0<<" Chg "<<big;
      float prevChg = big;
      // prepare to do the fit
      Point2_t inPt;
      Vector2_t outVec, outVecErr;
      float chgErr, chiDOF;
      // Initialize
      Fit2D(0, inPt, chgErr, outVec, outVecErr, chiDOF);
      unsigned short cnt = 0;
      for(unsigned short ii = 0; ii < tj.Pts.size(); ++ii) {
        short ipt = hiPt + ii * dir;
        if(ipt < tj.EndPt[0] || ipt > tj.EndPt[1]) break;
        TrajPoint& tp = tj.Pts[ipt];
        if(tp.Chg == 0) continue;
        // quit if the charge is much larger than the previous charge
        if(tp.Chg > 1.5 * prevChg) break;
        prevChg = tp.Chg;
        // Accumulate and save points
        inPt[0] = std::abs(tp.Pos[0] - wire0);
        inPt[1] = tp.Chg;
        // Assume 10% point-to-point charge fluctuations
        chgErr = 0.1 * tp.Chg;
        if(!Fit2D(2, inPt, chgErr, outVec, outVecErr, chiDOF)) break;
        if(tcc.dbgStp) mf::LogVerbatim("TC")<<ipt<<"  "<<PrintPos(slc, tp.Pos)<<" "<<inPt[0]<<" Chg "<<(int)tp.Chg;
        ++cnt;
        if(cnt == nPtsToCheck) break;
      } // ii
      if(cnt < 4) continue;
      // do the fit and get the results
      if(!Fit2D(-1, inPt, chgErr, outVec, outVecErr, chiDOF)) continue;
      // check for really bad chidof indicating a major failure
      if(chiDOF > 100) continue;
      unsigned short endPt = tj.EndPt[end];
      if(tj.Pts[endPt].AveChg > 0 && outVec[0] / tj.Pts[endPt].AveChg > 3) {
        //        std::cout<<"ChkStop: Crazy intcpt "<<intcpt<<" "<<tj.Pts[endPt].AveChg<<"\n";
        continue;
      }
      // The charge slope is negative for a stopping track in the way that the fit was constructed.
      // Flip the sign so we can make a cut against tcc.chkStopCuts[0] which is positive.
      outVec[1] = -outVec[1];
      if(outVec[1] > tcc.chkStopCuts[0] && chiDOF < tcc.chkStopCuts[2] && outVec[1] > 2 * outVecErr[1]) {
        tj.StopFlag[end][kBragg] = true;
        tj.AlgMod[kChkStop] = true;
        // Put the charge at the end into tp.AveChg
        tj.Pts[endPt].AveChg = outVec[0];
        // see if we can tag it as a proton
        std::vector<int> tjlist(1, tj.ID);
        float chgFrac = ChgFracNearPos(slc, tj.Pts[endPt].Pos, tjlist);
        if(chgFrac > 0.9) tj.PDGCode = 2212;
        if(prt) mf::LogVerbatim("TC")<<" end "<<end<<" fit chidof "<<chiDOF<<" slope "<<outVec[1]<<" +/- "<<outVecErr[1]<<" proton tag "<<tj.PDGCode;
      } else {
        if(prt) mf::LogVerbatim("TC")<<" end "<<end<<" fit chidof "<<chiDOF<<" slope "<<outVec[1]<<" +/- "<<outVecErr[1]<<" Not stopping";
      }
    } // end
    
  } // ChkStop

  //////////////////////TY://////////////////////////
  bool ChkMichel(TCSlice& slc, Trajectory& tj, unsigned short& lastGoodPt){
    
    if(!tcc.useAlg[kMichel]) return false;
    
    bool prt = (tcc.dbgStp || tcc.dbgAlg[kMichel]);
    
    //find number of hits that are consistent with Michel electron
    unsigned short nmichelhits = 0;
    //find number of hits that are consistent with Bragg peak
    unsigned short nbragghits = 0;
    float lastChg = 0;
    
    bool isfirsthit = true;
    unsigned short braggpeak = 0;
    
    for(unsigned short ii = 0; ii < tj.Pts.size(); ++ii) {
      if (ii>tj.EndPt[1]) continue;
      unsigned short ipt = tj.EndPt[1] - ii;
      if (tj.Pts[ipt].Chg>0){
        if (isfirsthit){
          isfirsthit = false;
          if (tj.Pts[ipt].ChgPull<0){
            ++nmichelhits;
          }
        }
        else{
          if (tj.Pts[ipt].ChgPull<0&&nmichelhits&&!nbragghits){//still Michel
            ++nmichelhits;
          }
          else{
            if (!nbragghits){
              ++nbragghits; //Last Bragg peak hit
              lastChg  = tj.Pts[ipt].Chg;
              braggpeak = ipt;
            }
            else if (tj.Pts[ipt].Chg<lastChg){ //still Bragg peak
              ++nbragghits;
              lastChg  = tj.Pts[ipt].Chg;
            }
            else break;
          }
        }
      }
    }
    if(prt) mf::LogVerbatim("TC")<<"ChkMichel Michel hits: "<<nmichelhits<<" Bragg peak hits: "<<nbragghits;
    if (nmichelhits>0&&nbragghits>2){//find Michel topology
      lastGoodPt = braggpeak;
      tj.AlgMod[kMichel] = true;
      return true;
    }
    else{
      return false;
    }
  }

  ////////////////////////////////////////////////
  void ChkHiChgHits(TCSlice& slc, CTP_t inCTP)
  {
    // Check allTraj trajectories in the current CTP to see if they are stopping
    if(!tcc.useAlg[kSplitHiChgHits]) return;
    
    for(size_t i = 0; i< slc.tjs.size(); ++i) {
      auto & tj = slc.tjs[i];
      if(tj.CTP != inCTP) continue;
      if(tj.AlgMod[kKilled]) continue;
      SplitHiChgHits(slc, tj);
    } // tj
    
  } // ChkHiChgHits
  
  /////////////////////TY:///////////////////////////
  void SplitHiChgHits(TCSlice& slc, Trajectory& tj){
    
    // Check allTraj trajectories in the current CTP and split high charge hits 
    if(!tcc.useAlg[kSplitHiChgHits]) return;
    // Only do it once
    if (tj.AlgMod[kSplitHiChgHits]) return;
    if(tj.AlgMod[kKilled]) return;
    //Ignore short trajectories
    if (tj.EndPt[1]<10) return;
    
    bool prt = (tcc.dbgStp || tcc.dbgAlg[kSplitHiChgHits]);
    
    for(unsigned short end = 0; end < 2; ++end) {
      if(prt) mf::LogVerbatim("TC")<<"SplitHiChghits "<<end<<" "<<tj.VtxID[end];
      float hichg = 0;
      unsigned short tp = tj.EndPt[end];
      unsigned short nlohits = 0;
      unsigned short lastHiTP = USHRT_MAX;
      while (tp != tj.EndPt[1-end]){
        float ptchg = TpSumHitChg(slc, tj.Pts[tp]);
        if (prt) mf::LogVerbatim("TC")<<"SplitHiChgHits "<<tp<<" "<<ptchg<<" "<<PrintPos(slc, tj.Pts[tp]);
        if (ptchg){
          if (tp == tj.EndPt[end]){
            hichg = ptchg;
            lastHiTP = tp;
          }
          else if (ptchg>0.4*hichg){
            if (!nlohits){
              hichg = ptchg;
              lastHiTP = tp;
            }
            else{
              break;
            }
          }
          else ++nlohits;
        }
        if (end==0){
          ++tp;
        }
        else{
          --tp;
        }
      }
      //if (tcc.dbgStp) mf::LogVerbatim("TC")<<"SplitHiChgHits "<<end<<" "<<nlohits;
      if (nlohits>4&&lastHiTP!=USHRT_MAX){
        //Create new vertex
        VtxStore aVtx;
        aVtx.Pos = tj.Pts[lastHiTP].Pos;
        aVtx.NTraj = 2;
        aVtx.Pass = tj.Pass;
        aVtx.Topo = 7;
        aVtx.ChiDOF = 0;
        aVtx.CTP = tj.CTP;
        aVtx.ID = slc.vtxs.size() + 1;
        if(!StoreVertex(slc, aVtx)) {
          if(prt) mf::LogVerbatim("TC")<<" Failed storing vertex "<<tj.VtxID[end];
          return;
        }
        
        // make a copy
        Trajectory newTj = tj;
        newTj.ID = slc.tjs.size() + 1;
        
        // keep high charge hits, reassign other hits to the new trajectory
        unsigned short tp1 = lastHiTP+1;
        if (end==1) tp1 = lastHiTP-1;
        for (unsigned short ipt = std::min(tj.EndPt[1-end], tp1); ipt <= std::max(tj.EndPt[1-end], tp1); ++ipt){
          tj.Pts[ipt].Chg = 0;
          for (unsigned short ii = 0; ii < tj.Pts[ipt].Hits.size(); ++ii) {
            if(!tj.Pts[ipt].UseHit[ii]) continue;
            unsigned int iht = tj.Pts[ipt].Hits[ii];
            // This shouldn't happen but check anyway
            if(slc.slHits[iht].InTraj != tj.ID) continue;
            slc.slHits[iht].InTraj = newTj.ID;
            tj.Pts[ipt].UseHit[ii] = false;
          }//ii
        }//ipt
        SetEndPoints(tj);
        tj.VtxID[1-end] = aVtx.ID;
        tj.AlgMod[kSplitHiChgHits] = true;
        if(prt) {
          mf::LogVerbatim("TC")<<"Splitting trajectory ID "<<tj.ID<<" new EndPts "<<tj.EndPt[0]<<" to "<<tj.EndPt[1];
        }
        
        for (unsigned short ipt = std::min(newTj.EndPt[end], lastHiTP); ipt <= std::max(newTj.EndPt[end], lastHiTP); ++ipt){
          newTj.Pts[ipt].Chg = 0;
          for (unsigned short ii = 0; ii < newTj.Pts[ipt].Hits.size(); ++ii) {
            newTj.Pts[ipt].UseHit[ii] = false;
          }//ii
        }//ipt
        SetEndPoints(newTj);
        newTj.VtxID[end] = aVtx.ID;
        newTj.AlgMod[kSplitHiChgHits] = true;
        slc.tjs.push_back(newTj);
        SetVx2Score(slc);
        
        break;     
      }
    }
  }

  //////////////////////////////////////////
  bool MakeJunkTraj(TCSlice& slc, std::vector<unsigned int> tHits)
  {
    
    if(!tcc.useAlg[kJunkTj]) return false;
    // Make a crummy trajectory using the provided hits
    
    if(tHits.size() < 2) return false;
    
    bool prt = (tcc.dbgStp || tcc.dbgAlg[kJunkTj]);
    std::vector<std::vector<unsigned int>> tpHits;
    
    // Start the trajectory using the first and last hits to
    // define a starting direction. Use the last pass settings
    Trajectory work;
    unsigned short pass = tcc.minPts.size() - 1;
    if(!StartTraj(slc, work, tHits[0], tHits[tHits.size()-1], pass)) return false;
    
    // Make TPs with the same separation as the wire spacing
    constexpr float pointSize = 1;
    
    // Do a more detailed specification of TPs if there
    // are enough hits
    if(tHits.size() > 6) {
      // fit all of the hits to a line
      Point2_t inPt;
      Vector2_t outVec, outVecErr;
      float inPtErr = 1, chiDOF;
      // initialize
      Fit2D(0, inPt, inPtErr, outVec, outVecErr, chiDOF);
      std::vector<Point2_t> fitPts(tHits.size());
      for(unsigned short ii = 0; ii < tHits.size(); ++ii) {
        unsigned int iht = tHits[ii];
        if(slc.slHits[iht].InTraj == SHRT_MAX) return false;
        auto& hit = (*evt.allHits)[slc.slHits[iht].allHitsIndex];
        inPt[0] = hit.WireID().Wire;
        inPt[1] = hit.PeakTime() * tcc.unitsPerTick;
        fitPts[ii] = inPt;
        // accumulate. Don't store the points since we don't care about chisq
        Fit2D(1, inPt, inPtErr, outVec, outVecErr, chiDOF);
      } // ii
      if(!Fit2D(-1, inPt, inPtErr, outVec, outVecErr, chiDOF)) return false;
      
      if(prt) mf::LogVerbatim("TC")<<" tHits line fit Angle "<<atan(outVec[1]);
      // A rough estimate of the trajectory angle
      work.Pts[0].Ang = atan(outVec[1]);
      work.Pts[0].Dir[0] = cos(work.Pts[0].Ang);
      work.Pts[0].Dir[1] = sin(work.Pts[0].Ang);
      SetAngleCode(work.Pts[0]);
      // Rotate the hits into this coordinate system to find the start and end
      // points and general direction
      double cs = cos(-work.Pts[0].Ang);
      double sn = sin(-work.Pts[0].Ang);
      float tAlong, minAlong = 1E6, maxAlong = -1E6;
      // sort the hits by the distance along the general direction
      std::vector<SortEntry> sortVec(tHits.size());
      SortEntry sortEntry;
      for(unsigned short ii = 0; ii < fitPts.size(); ++ii) {
        tAlong = cs * fitPts[ii][0] - sn * fitPts[ii][1];
        if(tAlong < minAlong) minAlong = tAlong;
        if(tAlong > maxAlong) maxAlong = tAlong;
        sortEntry.index = ii;
        sortEntry.val = tAlong;
        sortVec[ii] = sortEntry;
      } // ii
      std::sort(sortVec.begin(), sortVec.end(), valsDecreasing);
      // make a temp vector
      std::vector<unsigned int> tmp(sortVec.size());
      // overwrite with the sorted values
      for(unsigned short ii = 0; ii < sortVec.size(); ++ii) tmp[ii] = tHits[sortVec[ii].index];
      tHits = tmp;
      // create a trajectory point at each WSE unit (if there are hits at that point)
      unsigned short npts = (unsigned short)((maxAlong - minAlong) / pointSize);
      // rotate back into normal coordinate system
      if(prt) mf::LogVerbatim("TC")<<" minAlong "<<minAlong<<" maxAlong "<<maxAlong<<" work.Pts[0].Ang "<<work.Pts[0].Ang<<" npts "<<npts;
      if(npts < 2) npts = 2;
      tpHits.resize(npts);
      for(unsigned short ii = 0; ii < tHits.size(); ++ii) {
        unsigned short ipt = ((sortVec[ii].val - minAlong) / pointSize);
        if(ipt > npts - 1) ipt = npts - 1;
        if(prt) mf::LogVerbatim("TC")<<"tHit "<<PrintHit(slc.slHits[tHits[ii]])<<" length "<<sortVec[ii].val<<" ipt "<<ipt;
        if(tpHits[ipt].size() < 16) tpHits[ipt].push_back(tHits[ii]);
      }
    }  else {
      // just a few hits. Put each one at a TP in the order that
      // they were found
      tpHits.resize(tHits.size());
      for(unsigned short ii = 0; ii < tHits.size(); ++ii) tpHits[ii].push_back(tHits[ii]);
    } // tHits.size()
    // make the TPs
    // work.Pts[0] is already defined but it needs hits added
    work.Pts[0].Hits = tpHits[0];
    for(auto iht : tpHits[0]) slc.slHits[iht].InTraj = work.ID;
    // set all bits true
    work.Pts[0].UseHit.set();
    DefineHitPos(slc, work.Pts[0]);
    work.Pts[0].Pos = work.Pts[0].HitPos;
    if(prt) PrintTrajectory("MJT", slc, work, USHRT_MAX);
    // another TP to get the direction
    TrajPoint tpd;
    // make the rest of the TPs
    for(unsigned short ipt = 1; ipt < tpHits.size(); ++ipt) {
      if(tpHits[ipt].empty()) continue;
      // Use the previous TP as a starting point
      unsigned short lastPt = work.Pts.size() - 1;
      TrajPoint tp = work.Pts[lastPt];
      tp.Step = ipt;
      tp.Hits = tpHits[ipt];
      for(auto iht : tpHits[ipt]) slc.slHits[iht].InTraj = work.ID;
      // use all hits
      tp.UseHit.set();
      DefineHitPos(slc, tp);
      // Just use the hit position as the tj position
      tp.Pos = tp.HitPos;
      if(TrajPointSeparation(work.Pts[ipt-1], tp) < 0.5) {
        for(auto iht : tpHits[ipt]) slc.slHits[iht].InTraj = 0;
        continue;
      }
      work.Pts.push_back(tp);
      SetEndPoints(work);
    }
    if(prt) {
      PrintTrajectory("MJT", slc, work, USHRT_MAX);
    }
    work.AlgMod[kJunkTj] = true;
    work.IsGood = true;
    // Finally push it onto slc.tjs
    slc.isValid = StoreTraj(slc, work);
    if(!slc.isValid) {
      ReleaseHits(slc, work);
      return false;
    }
    if(tcc.useAlg[kChkInTraj]) {
      slc.isValid = InTrajOK(slc, "MJT");
      if(!slc.isValid) {
        ReleaseHits(slc, work);
        mf::LogVerbatim("TC")<<"InTrajOK failed in MakeJunkTraj";
        return false;
      }
    }
    return true;
  } // MakeJunkTraj


} // namespace tca