//
//  CubismUP_3D
//
//  Written by Guido Novati ( novatig@ethz.ch ).
//  This file started as an extension of code written by Wim van Rees
//  Copyright (c) 2017 ETHZ. All rights reserved.
//

#include "IF3D_NacaOperator.h"
#include "IF3D_FishLibrary.h"
#include "GenericOperator.h"

IF3D_NacaOperator::IF3D_NacaOperator(FluidGridMPI*g, ArgumentParser&p, const Real*const u)
: IF3D_FishOperator(g, p, u), bCreated(false)
{
  _parseArguments(p);
  const int Nextension = NEXTDX*NPPEXT;// up to 3dx on each side (to get proper interpolation up to 2dx)
  const double target_Nm = TGTPPB*length/vInfo[0].h_gridpoint;
  const double dx_extension = (1./NEXTDX)*vInfo[0].h_gridpoint;
  const int Nm = (Nextension+1)*(int)std::ceil(target_Nm/(Nextension+1.)) + 1;
  if (obstacleID) {
    printf("THIS OBSTACLE WORKS ONLY FOR SINGLE OBSTACLE!\n BLAME HIGH PUBLIC DEFICIT\n");
    MPI_Abort(grid->getCartComm(),0);
  }
  printf("%d %f %f\n",Nm,length,dx_extension);
  fflush(0);

  myFish = new NacaMidlineData(Nm, length,dx_extension);
}

void IF3D_NacaOperator::_parseArguments(ArgumentParser & parser)
{
  IF3D_FishOperator::_parseArguments(parser);
  absPos[0] = 0;
  bFixFrameOfRef[0] = true;
  bFixFrameOfRef[1] = false;
  bFixFrameOfRef[2] = false;
  bForcedInSimFrame[0] = false; 
  bForcedInSimFrame[1] = true; 
  bForcedInSimFrame[2] = true;// meaning that velocity cannot be changed by penalization
  #if 1
      Apitch = parser("-Apitch").asDouble(0.0); //aplitude of sinusoidal pitch angle
      Fpitch = parser("-Fpitch").asDouble(0.0); //frequency
      Ppitch = parser("-Ppitch").asDouble(0.0); //phase wrt to rowing motion
      Mpitch = parser("-Mpitch").asDouble(0.0); //mean angle
      Fheave = parser("-Fheave").asDouble(0.0);     //frequency of rowing motion
      Aheave = parser("-Aheave").asDouble(0.0); //amplitude
  #else
      ifstream reader("params.txt");
      if (reader.is_open()) {
        Apitch=0.0; Fpitch=0.0; Ppitch=0.0; Mpitch=0.0; Fheave=0.0; Aheave=0.0;
        reader >> Apitch; //aplitude of sinusoidal pitch angle
        reader >> Fpitch; //frequency
        reader >> Ppitch; //phase wrt to rowing motion
        reader >> Mpitch; //mean angle
        reader >> Fheave; //frequency of rowing motion
        reader >> Aheave; //amplitude of rowing motion
        reader.close();
      } else {
        cout << "Could not open params.txt" << endl;
        abort();
      }
  #endif
    Aheave *= length;
  if(!rank)
    printf("Naca: pos=%3.3f,Apitch=%3.3f,Fpitch=%3.3f,Ppitch=%3.3f,Mpitch=%3.3f,Frow=%3.3f,Arow=%3.3f\n",
    position[0],Apitch,Fpitch,Ppitch,Mpitch,Fheave,Aheave);
}

void IF3D_NacaOperator::update(const int stepID, const double t, const double dt, const Real* Uinf)
{
    //position[0]+= dt*transVel[0];
    //position[1]=    0.25 +          Aheave * std::cos(2*M_PI*Fheave*t);
    transVel[1]=-2*M_PI * Fheave * Aheave * std::sin(2*M_PI*Fheave*t);
    transVel[2] = 0;
    //position[2]+= dt*transVel[2];

    _2Dangle  =  Mpitch +          Apitch * std::cos(2*M_PI*(Fpitch*t+Ppitch));
    angVel[0] = 0;
    angVel[1] = 0;
    angVel[2] = -2*M_PI * Fpitch * Apitch * std::sin(2*M_PI*(Fpitch*t+Ppitch));
    quaternion[0] = std::cos(0.5*_2Dangle);
    quaternion[1] = 0;
    quaternion[2] = 0;
    quaternion[3] = std::sin(0.5*_2Dangle);

    Real velx_tot = transVel[0]-Uinf[0];
    Real vely_tot = transVel[1]-Uinf[1];
    Real velz_tot = transVel[2]-Uinf[2];
    absPos[0] += dt*velx_tot;
    absPos[1] += dt*vely_tot;
    absPos[2] += dt*velz_tot;

    sr.updateInstant(position[0], absPos[0], position[1], absPos[1],
                      _2Dangle, velx_tot, vely_tot, angVel[2]);

    _writeComputedVelToFile(stepID, t, Uinf);
}

void IF3D_NacaOperator::create(const int step_id,const double time, const double dt, const Real *Uinf)
{
  if (!bCreated)
  IF3D_FishOperator::create(step_id,time, dt, Uinf);
}

void IF3D_NacaOperator::finalize(const int step_id,const double time, const double dt, const Real *Uinf)
{
  if (!bCreated) {
  bCreated = true;
  IF3D_FishOperator::finalize(step_id,time, dt, Uinf);
  } else characteristic_function();
}
