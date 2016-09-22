//
//  IF3D_ObstacleVector.h
//  IncompressibleFluids3D
//
//  Created by Wim van Rees on 8/23/13.
//
//

#ifndef IncompressibleFluids3D_IF3D_ObstacleVector_h
#define IncompressibleFluids3D_IF3D_ObstacleVector_h

#include "IF3D_ObstacleOperator.h"

class IF3D_ObstacleVector : public IF3D_ObstacleOperator
{
protected:

    std::vector<IF3D_ObstacleOperator*> obstacles;

public:

    IF3D_ObstacleVector(FluidGridMPI* grid) : IF3D_ObstacleOperator(grid) {}
    IF3D_ObstacleVector(FluidGridMPI* grid, std::vector<IF3D_ObstacleOperator*> obstacles_in)
    : IF3D_ObstacleOperator(grid), obstacles(obstacles_in)
    {}
    ~IF3D_ObstacleVector();

    void characteristic_function() override;
    int nObstacles() const {return obstacles.size();}
    void computeVelocities(const double* Uinf) override;
    void update(const int step_id, const double t, const double dt, const double* Uinf) override;
    void restart(const double t, std::string filename = std::string()) override;
    void save(const int step_id, const double t, std::string filename = std::string()) override;
    std::vector<int> intersectingBlockIDs(const int buffer) const override;
    void computeDiagnostics(const int stepID, const double time, const double* Uinf, const double lambda) override;
    void computeForces(const int stepID, const double time, const double* Uinf, const double NU, const bool bDump) override;
    void create(const int step_id,const double time, const double dt, const double *Uinf) override;
    void execute(Communicator * comm, const int iAgent, const double time);
    void Accept(ObstacleVisitor * visitor) override;

    void addObstacle(IF3D_ObstacleOperator * obstacle)
    {
        obstacles.push_back(obstacle);
    }

    const std::vector<IF3D_ObstacleOperator*> & getObstacleVector() const
    {
        return obstacles;
    }

    Real getD() const override
    {
        abort();
        return -1;
    }
};

#endif