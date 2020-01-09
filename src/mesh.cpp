//#include "stdafx.h"
#include "fdtd.hpp"


int meshAndMark(fdtdMesh *sys, unordered_map<double, int> *xi1, unordered_map<double, int> *yi1, unordered_map<double, int> *zi1, tf::Subflow subflow) {
    /* Variables for all tasks */
    clock_t t0 = clock();
    tf::Task taskMM0 = subflow.placeholder();
    int lyr;
    double upper, lower;
    int status;
    double *xmin = new double[1];
    double *xmax = new double[1];
    double *ymin = new double[1];
    double *ymax = new double[1];
    myint *numNode = new myint[1];
    myint *numPortSides = new myint[1];
    myint *numOrigOldXY = new myint[1];
    myint *numOrigOldZ = new myint[1];
    unordered_map<double, int> *xi = xi1;
    unordered_map<double, int> *yi = yi1;
    unordered_map<double, int> *zi = zi1;

    /* Vectors for the old coordinates */
    vector<double> *xOrigOld = new vector<double>[1];
    vector<double> *yOrigOld = new vector<double>[1];
    vector<double> *zOrigOld = new vector<double>[1];
    double disMinx = MINDISFRACX * fmin((sys->ylim2 - sys->ylim1), (sys->xlim2 - sys->xlim1)); // Minimum discretization (m) retained in x-direction after node merging is fraction of smaller of x-extent or y-extent
    double disMiny = MINDISFRACY * fmin((sys->ylim2 - sys->ylim1), (sys->xlim2 - sys->xlim1)); // Minimum discretization (m) retained in y-direction after node merging is fraction of smaller of x-extent or y-extent
    double *disMinz = new double[1];
    double *disMaxx = new double[1];
    double *disMaxy = new double[1];
    double *minLayerDist = new double[1];
    *minLayerDist = (sys->zlim2 - sys->zlim1); // Initialize smallest distance between layers as entire domain height (m)
    clock_t *tt = new clock_t[1];
    *tt = clock();
    taskMM0.name("Initialize Vars for meshAndMark()");

    tf::Task taskMM1 = subflow.emplace([=]() {
        /* Generate the mesh nodes based on conductorIn information */
        numNode[0] = 0;
        for (myint indi = 0; indi < sys->numCdtRow; indi++) {
            numNode[0] += sys->conductorIn[indi].numVert;
        }
        //cout << numNode[0] << endl;

        /* Set up vectors for the old coordinates */
        *numPortSides = 0; // Total number of port sides (excitations counting port multiplicities)
        for (myint indi = 0; indi < sys->numPorts; indi++) {
            *numPortSides += sys->portCoor[indi].multiplicity;
        }
        *numOrigOldXY = numNode[0] + 2 * numPortSides[0]; // Number of coordinates reserved includes each conductor vertex and each pair of points defining area port side
        *numOrigOldZ = 2 * (sys->numStack + numPortSides[0]); // Number of coordinates reserved includes each layer start and stop coordinate and pair of points defining area port side
        xOrigOld->reserve(numOrigOldXY[0]);
        yOrigOld->reserve(numOrigOldXY[0]);
        zOrigOld->reserve(numOrigOldZ[0]);
    });
    taskMM1.name("Get Old Coord");
    taskMM0.precede(taskMM1);

    /* Populate vectors for the old coordinates */
    tf::Task taskMM2 = subflow.emplace([=]() {
        myint indj = 0;
        for (myint indi = 0; indi < sys->numCdtRow; indi++) {
            for (myint indk = 0; indk < sys->conductorIn[indi].numVert; indk++) {
                xOrigOld->push_back(sys->conductorIn[indi].x[indk]);
                yOrigOld->push_back(sys->conductorIn[indi].y[indk]);
                indj++; // Increase for each vertex on each conductor
            }
        }

        for (myint indi = 0; indi < sys->numPorts; indi++) {
            vector<double> x1coord = sys->portCoor[indi].x1;
            vector<double> y1coord = sys->portCoor[indi].y1;
            vector<double> x2coord = sys->portCoor[indi].x2;
            vector<double> y2coord = sys->portCoor[indi].y2;
            for (myint indk = 0; indk < sys->portCoor[indi].multiplicity; indk++) {
                xOrigOld->push_back(x1coord[indk]);
                yOrigOld->push_back(y1coord[indk]);
                indj++;
                xOrigOld->push_back(x2coord[indk]);
                yOrigOld->push_back(y2coord[indk]);
                indj++; // Increase for each point in area pair defining area port side
            }
        }
    });
    taskMM2.name("Populate Old XY");
    taskMM1.precede(taskMM2);

    tf::Task taskMM3 = subflow.emplace([=]() {
        myint indj = 0;
        for (myint indi = 0; indi < sys->numStack; indi++) {
            zOrigOld->push_back(sys->stackBegCoor[indi]);
            zOrigOld->push_back(sys->stackEndCoor[indi]);
            indj += 2; // Increase for each z-coordinate in area pair defining area layer
            if (sys->stackEndCoor[indi] - sys->stackBegCoor[indi] > 0) {
                minLayerDist[0] = fmin(minLayerDist[0], sys->stackEndCoor[indi] - sys->stackBegCoor[indi]); // Update smallest distance between layers as each layer processed (units included)
            }
        }
        for (myint indi = 0; indi < sys->numPorts; indi++) {
            vector<double> z1coord = sys->portCoor[indi].z1;
            vector<double> z2coord = sys->portCoor[indi].z2;
            for (myint indk = 0; indk < sys->portCoor[indi].multiplicity; indk++) {
                zOrigOld->push_back(z1coord[indk]);
                zOrigOld->push_back(z2coord[indk]);
                indj += 2; // Increase for each point in area pair defining area port side
            }
        }
    });
    taskMM3.name("Populate Old Z");
    taskMM1.precede(taskMM3);

    /*******************************************************************************************/
    /* Discretize domain in the x-direction */
    tf::Task taskMM4 = subflow.emplace([=]() {
        sort(xOrigOld->begin(), xOrigOld->end());
        sys->nx = 1;
        xmin[0] = xOrigOld->at(0);
        xmax[0] = xOrigOld->at(numOrigOldXY[0] - 1);
        *disMaxx = MAXDISFRACX * (sys->xlim2 - sys->xlim1); // Maximum discretization distance in x-direction is fraction of x-extent
        myint xMaxInd = (myint)((xmax[0] - xmin[0]) / *disMaxx); // Cast to myint after floating-point division

        for (myint indi = 1; indi < numOrigOldXY[0]; indi++) {
            if (abs(xOrigOld->at(indi) - xOrigOld->at(indi - 1)) > disMinx) {
                sys->nx++;
            }
        }
        double *xn = (double*)calloc(numNode[0] + 6 * numPortSides[0] + xMaxInd, sizeof(double)); // Initialize saved grid node coordinates for worst case insertions
        xn[0] = xOrigOld->at(0);
        double temp = xn[0];
        myint indj = 0;
        sys->nx = 1;
        for (myint indi = 1; indi < numOrigOldXY[0]; indi++) {
            if (abs(xOrigOld->at(indi) - temp) > disMinx && abs(xOrigOld->at(indi) - temp) <= *disMaxx) {
                indj++;
                xn[indj] = xOrigOld->at(indi); // Save coordinate for nodes to keep if in discretization retention range
                temp = xn[indj];
                sys->nx++;
            }
            else if (abs(xOrigOld->at(indi) - temp) > disMinx && abs(xOrigOld->at(indi) - temp) > *disMaxx) {
                while (abs(xOrigOld->at(indi) - temp) > *disMaxx) {
                    indj++;
                    xn[indj] = xn[indj - 1] + *disMaxx; // Save coordinates of all nodes past maximum discretization at intervals of the maximum discretization past the previous coordinate
                    temp = xn[indj];
                    sys->nx++;
                }
                if (abs(xOrigOld->at(indi) - temp) > disMinx) { // Check original coordinate against new temp coordinate
                    sys->nx++;
                    temp = xOrigOld->at(indi);
                }
                indj++;
                xn[indj] = xOrigOld->at(indi); // Include the original coordinate as area node
            }
            else {
                indj++;
                xn[indj] = xOrigOld->at(indi); // Keep original coordinate as area node even if it is smaller than minimum discretization?
            }
        }
        myint countx = indj;

        indj = 0;
        double first, second;
        temp = xn[0];
        for (myint indi = 1; indi <= countx; indi++) {    // Set the discretization length around port to be equal
            if (abs(xn[indi] - temp) > disMinx) {
                indj++;
                temp = xn[indi];
            }
        }
        sys->nx = indj + 1;

        /* Mesh refinement for x-direction */
        sort(xn, xn + countx + 1);
        xi->clear();
        sys->xn = (double*)calloc(sys->nx, sizeof(double));
        indj = 0;
        sys->xn[0] = xn[0];
        temp = sys->xn[0];
        xi->insert_or_assign(sys->xn[0], indj);
        for (myint indi = 1; indi <= countx; indi++) {    // Set the discretization length around port to be equal
            if (abs(xn[indi] - temp) > disMinx) {
                indj++;
                sys->xn[indj] = xn[indi];
                temp = sys->xn[indj];
                xi->insert_or_assign(sys->xn[indj], indj);
            }
            else {
                xi->insert_or_assign(xn[indi], indj);
            }
        }
        sys->nx = indj + 1;

        /* Reduce memory usage for x-direction original coordinates */
        free(xn);
        xOrigOld->clear();
        xOrigOld->shrink_to_fit();
    });
    taskMM4.name("Discretize x-direction");
    taskMM2.precede(taskMM4);
    taskMM3.precede(taskMM4);

    /***************************************************************************/
    /* Discretize domain in the y-direction */
    tf::Task taskMM5 = subflow.emplace([=]() {
        sort(yOrigOld->begin(), yOrigOld->end());
        sys->ny = 1;
        ymin[0] = yOrigOld->at(0);
        ymax[0] = yOrigOld->at(numOrigOldXY[0] - 1);
        *disMaxy = MAXDISFRACY * (sys->ylim2 - sys->ylim1); // Maximum discretization distance in y-direction is fraction of y-extent
        myint yMaxInd = (myint)((ymax[0] - ymin[0]) / *disMaxy); // Cast to myint after floating-point division

        for (myint indi = 1; indi < numOrigOldXY[0]; indi++) {
            if (abs(yOrigOld->at(indi) - yOrigOld->at(indi - 1)) > disMiny) {
                sys->ny++;
            }
        }
        double *yn = (double*)calloc(numNode[0] + 6 * numPortSides[0] + yMaxInd, sizeof(double));
        yn[0] = yOrigOld->at(0);
        myint indj = 0;
        sys->ny = 1;
        double temp = yn[0];
        for (myint indi = 1; indi < numOrigOldXY[0]; indi++) {
            if (abs(yOrigOld->at(indi) - temp) > disMiny && abs(yOrigOld->at(indi) - temp) <= *disMaxy) {
                indj++;
                yn[indj] = yOrigOld->at(indi);
                temp = yn[indj];
                sys->ny++;
            }
            else if (abs(yOrigOld->at(indi) - temp) > disMiny && abs(yOrigOld->at(indi) - temp) > *disMaxy) {
                while (abs(yOrigOld->at(indi) - temp) > *disMaxy) {
                    indj++;
                    yn[indj] = yn[indj - 1] + *disMaxy;
                    temp = yn[indj];
                    sys->ny++;
                }
                if (abs(yOrigOld->at(indi) - temp) > disMiny) {
                    sys->ny++;
                    temp = yOrigOld->at(indi);
                }
                indj++;
                yn[indj] = yOrigOld->at(indi);
            }
            else {
                indj++;
                yn[indj] = yOrigOld->at(indi);
            }
        }

        myint county = indj;

        indj = 0;
        temp = yn[0];
        for (myint indi = 1; indi <= county; indi++) {    // Set the discretization length around port to be equal
            if (abs(yn[indi] - temp) > disMiny) {
                indj++;
                temp = yn[indi];

            }
        }
        sys->ny = indj + 1;

        /* Mesh refinement for y-direction */
        sort(yn, yn + county + 1);
        yi->clear();
        sys->yn = (double*)calloc(sys->ny, sizeof(double));
        indj = 0;
        sys->yn[0] = yn[0];
        temp = sys->yn[0];
        yi->insert_or_assign(sys->yn[0], indj);
        for (myint indi = 1; indi <= county; indi++) {    // Set the discretization length around port to be equal
            if (abs(yn[indi] - temp) > disMiny) {
                indj++;
                sys->yn[indj] = yn[indi];
                temp = sys->yn[indj];
                yi->insert_or_assign(sys->yn[indj], indj);
            }
            else {
                yi->insert_or_assign(yn[indi], indj);
            }
        }
        sys->ny = indj + 1;

        /* Reduce memory usage for y-direction original coordinates */
        yOrigOld->clear();
        yOrigOld->shrink_to_fit();
    });
    taskMM5.name("Discretize y-direction");
    taskMM2.precede(taskMM5);
    taskMM3.precede(taskMM5);

    /********************************************************************************/
    /* Discretize domain in the z-direction */
    tf::Task taskMM6 = subflow.emplace([=]() {
        sort(zOrigOld->begin(), zOrigOld->end());
        sys->nz = 1;
        *disMinz = minLayerDist[0] * MINDISFRACZ; // Minimum discretization retained in z-direction after node merging is fraction of smallest distance between layers
        //double disMaxz = minLayerDist[0] / MAXDISLAYERZ; // Maximum discretization distance in z-direction is fraction of closest distance between layers
        double *zn = (double*)calloc(2 * sys->numStack + 6 * numPortSides[0], sizeof(double));
        zn[0] = zOrigOld->at(0);
        myint indj = 0;
        for (myint indi = 1; indi < numOrigOldZ[0]; indi++) {
            if (abs(zOrigOld->at(indi) - zOrigOld->at(indi - 1)) > *disMinz) {
                sys->nz++;
            }
            indj++;
            zn[indj] = zOrigOld->at(indi); // Save coordinates for nodes
        }
        myint countz = numOrigOldZ[0] - 1;

        /* Mesh refinement in z-direction */
        sort(zn, zn + countz + 1);
        zi->clear();
        sys->zn = (double*)calloc(sys->nz, sizeof(double));
        indj = 0;
        sys->zn[0] = zn[0];
        zi->insert_or_assign(sys->zn[0], indj);
        for (myint indi = 1; indi <= countz; indi++) {    // Set the discretization length around port to be equal
            if (abs(zn[indi] - zn[indi - 1]) > *disMinz) {
                indj++;
                sys->zn[indj] = zn[indi];
                zi->insert_or_assign(sys->zn[indj], indj);
            }
            else {
                zi->insert_or_assign(zn[indi], indj);
            }
        }
        sys->nz = indj + 1;
        free(zn);
    });
    taskMM6.name("Discretize z-direction");
    taskMM2.precede(taskMM6);
    taskMM3.precede(taskMM6);

    /********************************************************************************/
    /* Putting the layer relative permittivities and conductivities in order of increasing z-coordinate */
    tf::Task taskMM7 = subflow.emplace([=]() {
        myint indi = 0;
        if (sys->stackBegCoor[0] == sys->zn[0]) {
            myint indj = 0;
            sys->stackEpsn.push_back(sys->stackEps[indj]);    // the stack eps with indi < sys->N_edge_s
            sys->stackSign.push_back(sys->stackSig[indj]);    // the stack sig with indi < sys->N_edge_s
            while (indi < sys->nz - 1) {
                if ((sys->zn[indi] + sys->zn[indi + 1]) / 2 >= sys->stackBegCoor[indj] && (sys->zn[indi] + sys->zn[indi + 1]) / 2 <= sys->stackEndCoor[indj]) {
                    sys->stackEpsn.push_back(sys->stackEps[indj]);
                    sys->stackSign.push_back(sys->stackSig[indj]);
                    indi++;
                }
                else {
                    indj++;
                }
            }
        }
        else {
            myint indj = sys->numStack - 1;
            sys->stackEpsn.push_back(sys->stackEps[indj]);
            sys->stackSign.push_back(sys->stackSig[indj]);
            while (indi < sys->nz - 1) {
                if ((sys->zn[indi] + sys->zn[indi + 1]) / 2 >= sys->stackBegCoor[indj] && (sys->zn[indi] + sys->zn[indi + 1]) / 2 <= sys->stackEndCoor[indj]) {
                    sys->stackEpsn.push_back(sys->stackEps[indj]);
                    sys->stackSign.push_back(sys->stackSig[indj]);
                    indi++;
                }
                else {
                    indj--;
                }
            }
        }

        /* Reduce memory usage for z-direction original coordinates */
        zOrigOld->clear();
        zOrigOld->shrink_to_fit();
    });
    taskMM7.name("Sort epsilon_{r} and sigma");
    taskMM6.precede(taskMM7);

    tf::Task taskMM8 = subflow.emplace([=]() {
#ifdef SKIP_WRITE_SYS_TO_FILE
        sys->stackEndCoor.clear();
        sys->stackEndCoor.shrink_to_fit();
        sys->stackBegCoor.clear();
        sys->stackBegCoor.shrink_to_fit();
#endif

#ifdef PRINT_NODE_COORD
        /*for (myint indi = 0; indi < sys->nz - 1; indi++) {
            cout << sys->stackEpsn[indi] << endl;
        }*/
        ofstream outc;
        outc.open("x.txt", std::ofstream::out | std::ofstream::trunc);
        for (myint indi = 0; indi < sys->nx; indi++) {
            outc << sys->xn[indi] << " ";
        }
        outc.close();
        outc.open("y.txt", std::ofstream::out | std::ofstream::trunc);
        for (myint indi = 0; indi < sys->ny; indi++) {
            outc << sys->yn[indi] << " ";
        }
        outc.close();

        for (myint indi = 0; indi < sys->nz; indi++) {
            cout << sys->zn[indi] << " ";
        }
        cout << endl << endl;
#endif

        /***********************************************************************************************/
        /* Save counts of the final discretization */
        sys->N_cell_x = sys->nx - (myint)1;
        sys->N_cell_y = sys->ny - (myint)1;
        sys->N_cell_z = sys->nz - (myint)1;

        sys->N_edge_s = sys->N_cell_y*(sys->N_cell_x + 1) + sys->N_cell_x*(sys->N_cell_y + 1);
        sys->N_edge_v = (sys->N_cell_x + 1)*(sys->N_cell_y + 1);
        sys->N_edge = sys->N_edge_s*(sys->N_cell_z + 1) + sys->N_edge_v*(sys->N_cell_z);

        sys->N_node_s = sys->N_edge_v;
        sys->N_node = (sys->N_cell_z + 1)*sys->N_node_s;

        sys->N_patch_s = sys->N_cell_x*sys->N_cell_y;
        sys->N_patch_v = (sys->N_cell_x + 1)*sys->N_cell_y + (sys->N_cell_y + 1)*sys->N_cell_x;
        sys->N_patch = sys->N_patch_s*(sys->N_cell_z + 1) + sys->N_patch_v*sys->N_cell_z;

        sys->markEdge = (myint*)calloc(sys->N_edge, sizeof(myint));   // Mark which conductor index given edge is inside
        sys->markNode = (myint*)calloc(sys->N_node, sizeof(myint));   // Mark which conductor index given node is inside

#ifdef PRINT_VERBOSE_TIMING
        cout << "The time to read and assign x, y, z coordinates is " << (clock() - *tt) * 1.0 / CLOCKS_PER_SEC << " s" << endl;
#endif

#ifdef PRINT_DIS_COUNT
        cout << endl;
        cout << "Discretization information: " << endl;
        cout << " disMinx  = " << disMinx << " m" << endl;
        cout << " disMiny  = " << disMiny << " m" << endl;
        cout << " disMinz  = " << *disMinz << " m" << endl;
        cout << " disMaxx  = " << *disMaxx << " m" << endl;
        cout << " disMaxy  = " << *disMaxy << " m" << endl;
        //cout << " disMaxz  = " << disMaxz << " m" << endl;
        cout << endl;
        cout << " N_edge   = " << sys->N_edge << endl;
        cout << " N_node   = " << sys->N_node << endl;
        cout << " N_cell_x = " << sys->N_cell_x << endl;
        cout << " N_cell_y = " << sys->N_cell_y << endl;
        cout << " N_cell_z = " << sys->N_cell_z << endl;
        cout << endl;
#endif

        /* Discretization warnings */
        if (sys->N_cell_x <= 1)
        {
            cerr << "Failed to generate mesh with more than one element in the x-direction. Check value of disMin. Aborting now." << endl;
            exit(1);
        }
        if (sys->N_cell_y <= 1)
        {
            cerr << "Failed to generate mesh with more than one element in the y-direction. Check value of disMin. Aborting now." << endl;
            exit(1);
        }
        });
    taskMM8.name("Report Discretization");
    taskMM4.precede(taskMM8);
    taskMM5.precede(taskMM8);
    taskMM7.precede(taskMM8);

    /***********************************************************************************************/
    /* Establish index relationship between nodes and conductors */
    double xc, yc;
    myint ys, yl;

    *tt = clock();

    tf::Task taskMM9 = subflow.emplace([=]() {
        // sys->printConductorIn();
        /* Fast algorithm to find nodes inside conductors */
        sys->findInsideCond(*xi, *yi, *zi);

        /*double px = -3225.6e-6, py = 806.4e-6, pz = 716e-6;
        sys->checkPoint(px, py, pz, *xi, *yi, *zi);*/
    });
    taskMM9.name("Find Conductor Nodes");
    taskMM8.precede(taskMM9);

    /* Assign markers to nodes and edges beyond included conductors */
    tf::Task taskMM10 = subflow.emplace([=] () {
#ifdef LOWER_BOUNDARY_PEC
        for (myint indi = 0; indi < sys->N_edge_s; indi++) {    // the lower PEC plane
            sys->markEdge[indi] = sys->numCdtRow + (myint)1;
        }
        for (myint indi = 0; indi < sys->N_node_s; indi++) {
            sys->markNode[indi] = (myint)1;
        }
#endif
#ifdef UPPER_BOUNDARY_PEC
        for (myint indi = sys->N_edge - sys->N_edge_s; indi < sys->N_edge; indi++) {    // the upper PEC plane
            sys->markEdge[indi] = sys->numCdtRow + (myint)2;
        }
        for (myint indi = sys->N_node - sys->N_node_s; indi < sys->N_node; indi++) {
            sys->markNode[indi] = (myint)1;
        }
#endif
    });
    taskMM10.name("Mark Edges and Nodes");
    taskMM9.precede(taskMM10);

    /* Experimental: Store PEC planes as CdtRow to avoid segfault*/
    /* fdtdOneCondct class lacks area parametrized constructor or GDSII layer numbers to make this easy */
    /* do NOT update sys->numCdtRow afterwards */
    tf::Task taskMM11 = subflow.emplace([=]() {
        double xOuter[4] = { sys->xlim2, sys->xlim2, sys->xlim1, sys->xlim1 }; // Lower-right around counter-clockwise
        double yOuter[4] = { sys->ylim1, sys->ylim2, sys->ylim2, sys->ylim1 }; // Lower-right around counter-clockwise
        int layerMin = 65536;
        int layerMax = -1;
        for (size_t indi = 0; indi < sys->conductorIn.size(); indi++)
        {
            if (sys->conductorIn[indi].layer < layerMin)
            {
                layerMin = sys->conductorIn[indi].layer;
            }
            else if (sys->conductorIn[indi].layer > layerMax)
            {
                layerMax = sys->conductorIn[indi].layer;
            }
        }

        /*ofstream out;
        out.open("markEdge.txt", std::ofstream::out | std::ofstream::trunc);
        for (myint indi = 0; indi < sys->N_edge; indi++){
            out << sys->markEdge[indi] << endl;
        }
        out.close();*/
#ifdef LOWER_BOUNDARY_PEC
        sys->conductorIn.push_back(fdtdOneCondct()); // the lower PEC plane
        sys->conductorIn.back().numVert = 4;
        sys->conductorIn.back().xmax = sys->xlim2;
        sys->conductorIn.back().xmin = sys->xlim1;
        sys->conductorIn.back().ymax = sys->ylim2;
        sys->conductorIn.back().ymin = sys->ylim1;
        sys->conductorIn.back().x = xOuter;
        sys->conductorIn.back().y = yOuter;
        //sys->conductorIn.back().layer = layerMin;
        sys->conductorIn.back().zmax = sys->zn[0];
        sys->conductorIn.back().zmin = sys->zn[0];
#endif
#ifdef UPPER_BOUNDARY_PEC
        sys->conductorIn.push_back(fdtdOneCondct()); // the upper PEC plane
        sys->conductorIn.back().numVert = 4;
        sys->conductorIn.back().xmax = sys->xlim2;
        sys->conductorIn.back().xmin = sys->xlim1;
        sys->conductorIn.back().ymax = sys->ylim2;
        sys->conductorIn.back().ymin = sys->ylim1;
        sys->conductorIn.back().x = xOuter;
        sys->conductorIn.back().y = yOuter;
        //sys->conductorIn.back().layer = layerMax;
        sys->conductorIn.back().zmax = sys->zn[sys->nz - 1];
        sys->conductorIn.back().zmin = sys->zn[sys->nz - 1];
#endif
    });
    taskMM11.name("Set PEC Planes");
    taskMM9.precede(taskMM11);

    /* construct edgelink, no need for edgelink */
    tf::Task taskMM12 = subflow.emplace([=]() {
        myint eno;
        //sys->edgelink = (myint*)malloc(2 * sizeof(myint)*sys->N_edge);
        //for (lyr = 1; lyr <= sys->N_cell_z + 1; lyr++) {
        //    for (myint indi = 1; indi <= sys->N_cell_x + 1; indi++) {    //edge along y axis
        //        for (myint indj = 1; indj <= sys->N_cell_y; indj++) {
        //            eno = (lyr - 1)*(sys->N_edge_s + sys->N_edge_v) + (indi - 1)*sys->N_cell_y + indj;
        //            sys->edgelink[(eno - 1) * 2 + 1 - 1] = (lyr - 1)*sys->N_node_s + (indi - 1)*(sys->N_cell_y + 1) + indj - 1;
        //            sys->edgelink[(eno - 1) * 2 + 2 - 1] = (lyr - 1)*sys->N_node_s + (indi - 1)*(sys->N_cell_y + 1) + indj;

        //        }
        //    }
        //    for (myint indi = 1; indi <= sys->N_cell_x; indi++) {    //edge along x axis
        //        for (myint indj = 1; indj <= sys->N_cell_y + 1; indj++) {
        //            eno = (lyr - 1)*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_x + 1)*sys->N_cell_y + (indi - 1)*(sys->N_cell_y + 1) + indj;
        //            sys->edgelink[(eno - 1) * 2 + 1 - 1] = (lyr - 1)*sys->N_node_s + (indi - 1)*(sys->N_cell_y + 1) + indj - 1;
        //            sys->edgelink[(eno - 1) * 2 + 2 - 1] = (lyr - 1)*sys->N_node_s + indi*(sys->N_cell_y + 1) + indj - 1;

        //        }
        //    }
        //}
        //for (lyr = 1; lyr <= sys->N_cell_z; lyr++) {    // edge along z axis
        //    for (myint indi = 1; indi <= sys->N_cell_x + 1; indi++) {
        //        for (myint indj = 1; indj <= sys->N_cell_y + 1; indj++) {
        //            eno = (lyr - 1)*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + (indi - 1)*(sys->N_cell_y + 1) + indj;
        //            sys->edgelink[(eno - 1) * 2 + 1 - 1] = (lyr - 1)*sys->N_node_s + (indi - 1)*(sys->N_cell_y + 1) + indj - 1;
        //            sys->edgelink[(eno - 1) * 2 + 2 - 1] = lyr*sys->N_node_s + (indi - 1)*(sys->N_cell_y + 1) + indj - 1;
        //        }
        //    }
        //}

        /* construct nodepos, no need for nodepos */
        myint nno;

        //sys->nodepos = (double*)malloc(sizeof(double)*sys->N_node * 3);   //N_node rows and 3 columns, input row by row
        //for (lyr = 1; lyr <= sys->N_cell_z + 1; lyr++) {
        //    for (myint indi = 1; indi <= sys->N_cell_x + 1; indi++) {
        //        for (myint indj = 1; indj <= sys->N_cell_y + 1; indj++) {
        //            nno = (lyr - 1)*sys->N_node_s + (indi - 1)*(sys->N_cell_y + 1) + indj;
        //            sys->nodepos[(nno - 1) * 3 + 1 - 1] = sys->xn[indi - 1];
        //            sys->nodepos[(nno - 1) * 3 + 2 - 1] = sys->yn[indj - 1];
        //            sys->nodepos[(nno - 1) * 3 + 3 - 1] = sys->zn[lyr - 1];
        //        }
        //    }
        //}

        /* construct nodeEdge */
        double leng;
        myint inz, inx, iny;
        myint node1, node2;

        /* no need for nodeEdge and nodeEdgea */
        //vector<pair<myint, double> > area;
        //for (myint indi = 0; indi < sys->N_node; indi++) {
        //    sys->nodeEdge.push_back(area);
        //    sys->nodeEdgea.push_back(area);
        //}
        //for (myint indi = 0; indi < sys->N_edge; indi++) {
        //    status = compute_edgelink(sys, indi, node1, node2);
        //    if (indi % (sys->N_edge_s + sys->N_edge_v) >= sys->N_edge_s) {    // this edge is along z axis
        //        inz = indi / (sys->N_edge_s + sys->N_edge_v);
        //        leng = sys->zn[inz + 1] - sys->zn[inz];
        //    }
        //    else if (indi % (sys->N_edge_s + sys->N_edge_v) >= (sys->N_cell_y) * (sys->N_cell_x + 1)) {    // this edge is along x axis
        //        inx = ((indi % (sys->N_edge_s + sys->N_edge_v)) - (sys->N_cell_y) * (sys->N_cell_x + 1)) / (sys->N_cell_y + 1);
        //        leng = sys->xn[inx + 1] - sys->xn[inx];
        //    }
        //    else {    // this edge is along y axis
        //        iny = (indi % (sys->N_edge_s + sys->N_edge_v)) % sys->N_cell_y;
        //        leng = sys->yn[iny + 1] - sys->yn[iny];
        //    }

        //    /*leng = pow((sys->nodepos[sys->edgelink[indi * 2] * 3] - sys->nodepos[sys->edgelink[indi * 2 + 1] * 3]), 2);
        //    leng = leng + pow((sys->nodepos[sys->edgelink[indi * 2] * 3 + 1] - sys->nodepos[sys->edgelink[indi * 2 + 1] * 3 + 1]), 2);
        //    leng = leng + pow((sys->nodepos[sys->edgelink[indi * 2] * 3 + 2] - sys->nodepos[sys->edgelink[indi * 2 + 1] * 3 + 2]), 2);
        //    leng = sqrt(leng);*/
        //    sys->nodeEdge[node1].push_back(make_pair(indi, 1 / leng));
        //    sys->nodeEdge[node2].push_back(make_pair(indi, -1 / leng));
        //}
        //cout << "nodeEdge is done" << endl;
        //int ix, iy, iz;
        //iz = sys->N_cell_z;
        //for (ix = 0; ix < sys->nx; ix++) {
        //    for (iy = 0; iy < sys->ny; iy++) {
        //        sys->nodeEdgea[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy].push_back(make_pair((iz - 1) * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + ix * (sys->N_cell_y + 1) + iy, -1 / (sys->zn[iz] - sys->zn[iz - 1])));
        //    }
        //}
        //iz = 0;
        //for (ix = 0; ix < sys->nx; ix++) {
        //    for (iy = 0; iy < sys->ny; iy++) {
        //        sys->nodeEdgea[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy].push_back(make_pair(iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + ix * (sys->N_cell_y + 1) + iy, 1 / (sys->zn[iz + 1] - sys->zn[iz])));
        //    }
        //}
        //for (iz = 1; iz < sys->N_cell_z; iz++) {
        //    for (ix = 0; ix < sys->nx; ix++) {
        //        for (iy = 0; iy < sys->ny; iy++) {
        //            sys->nodeEdgea[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy].push_back(make_pair((iz - 1) * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + ix * (sys->N_cell_y + 1) + iy, -2 / (sys->zn[iz + 1] - sys->zn[iz - 1])));
        //            sys->nodeEdgea[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy].push_back(make_pair(iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + ix * (sys->N_cell_y + 1) + iy, 2 / (sys->zn[iz + 1] - sys->zn[iz - 1])));
        //        }
        //    }
        //}
        //ix = sys->N_cell_x;
        //for (iz = 0; iz < sys->nz; iz++) {
        //    for (iy = 0; iy < sys->ny; iy++) {
        //        sys->nodeEdgea[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy].push_back(make_pair(iz * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_x + 1) * sys->N_cell_y + (ix - 1) * (sys->N_cell_y + 1) + iy, -1 / (sys->xn[ix] - sys->xn[ix - 1])));
        //    }
        //}
        //ix = 0;
        //for (iz = 0; iz < sys->nz; iz++) {
        //    for (iy = 0; iy < sys->ny; iy++) {
        //        sys->nodeEdgea[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy].push_back(make_pair(iz * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_x + 1) * sys->N_cell_y + ix * (sys->N_cell_y + 1) + iy, 1 / (sys->xn[ix + 1] - sys->xn[ix])));
        //    }
        //}
        //for (ix = 1; ix < sys->N_cell_x; ix++) {
        //    for (iz = 0; iz < sys->nz; iz++) {
        //        for (iy = 0; iy < sys->ny; iy++) {
        //            sys->nodeEdgea[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy].push_back(make_pair(iz * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_x + 1) * sys->N_cell_y + (ix - 1) * (sys->N_cell_y + 1) + iy, -2 / (sys->xn[ix + 1] - sys->xn[ix - 1])));
        //            sys->nodeEdgea[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy].push_back(make_pair(iz * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_x + 1) * sys->N_cell_y + ix * (sys->N_cell_y + 1) + iy, 2 / (sys->xn[ix + 1] - sys->xn[ix - 1])));
        //        }
        //    }
        //}
        //iy = sys->N_cell_y;
        //for (iz = 0; iz < sys->nz; iz++) {
        //    for (ix = 0; ix < sys->nx; ix++) {
        //        sys->nodeEdgea[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy].push_back(make_pair(iz * (sys->N_edge_s + sys->N_edge_v) + ix * sys->N_cell_y + iy - 1, -1 / (sys->yn[iy] - sys->yn[iy - 1])));
        //    }
        //}
        //iy = 0;
        //for (iz = 0; iz < sys->nz; iz++) {
        //    for (ix = 0; ix < sys->nx; ix++) {
        //        sys->nodeEdgea[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy].push_back(make_pair(iz * (sys->N_edge_s + sys->N_edge_v) + ix * sys->N_cell_y + iy, 1 / (sys->yn[iy + 1] - sys->yn[iy])));
        //    }
        //}
        //for (iy = 1; iy < sys->N_cell_y; iy++) {
        //    for (iz = 0; iz < sys->nz; iz++) {
        //        for (ix = 0; ix < sys->nx; ix++) {
        //            sys->nodeEdgea[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy].push_back(make_pair(iz * (sys->N_edge_s + sys->N_edge_v) + ix * sys->N_cell_y + iy - 1, -2 / (sys->yn[iy + 1] - sys->yn[iy - 1])));
        //            sys->nodeEdgea[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy].push_back(make_pair(iz * (sys->N_edge_s + sys->N_edge_v) + ix * sys->N_cell_y + iy, 2 / (sys->yn[iy + 1] - sys->yn[iy - 1])));
        //        }
        //    }
        //}
    });
    taskMM12.name("Deprecated edgelink and nodeedge");
    taskMM8.precede(taskMM12);

    /* Implement breadth-first search (BFS) */
    myint **visited = new myint*[1];
    unordered_set<int> base;

    /* BFS to figure out the disjoint conductors */
    tf::Task taskMM13 = subflow.emplace([=] () {
        visited[0] = (myint*)calloc(sys->N_node, sizeof(myint));
        *tt = clock();
        queue<myint> qu;
        myint count = (myint)0;
        myint eno = (myint)0;
        myint node1 = (myint)0;
        myint node2 = (myint)0;
        for (myint indi = 0; indi < sys->N_node; indi++) {
            if (sys->markNode[indi] == 0) {
                continue;
            }
            else {
                if (visited[0][indi] != 0) {
                    continue;
                }
                else {
                    qu.push(indi);
                    count++;
                    visited[0][indi] = count;
                    //sys->cond2condIn.push_back(base);
                    while (!qu.empty()) {
                        myint inz = qu.front() / (sys->N_node_s);
                        myint inx = (qu.front() % sys->N_node_s) / (sys->N_cell_y + 1);
                        myint iny = (qu.front() % sys->N_node_s) % (sys->N_cell_y + 1);

                        // How many edges this node connects to
                        if (inz != 0) {    // this node is not on the bottom plane
                            eno = (inz - 1) * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx * (sys->N_cell_y + 1) + iny;    // the node's lower edge
                            if (sys->markEdge[eno] != 0) {
                                sys->compute_edgelink(eno, node1, node2);    // compute_edgelink is used to get edge eno's two side's nodes node1, node2
                                if ((node1 != qu.front() && visited[0][node1] == 0)) {
                                    visited[0][node1] = count;
                                    qu.push(node1);
                                }
                                else if ((node2 != qu.front() && visited[0][node2] == 0)) {
                                    visited[0][node2] = count;
                                    qu.push(node2);
                                }
                            }
                        }
                        if (inz != sys->nz - 1) {    // this node is not on the upper plane
                            eno = inz * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx * (sys->N_cell_y + 1) + iny;    // the node's lower edge
                            if (sys->markEdge[eno] != 0) {
                                sys->compute_edgelink(eno, node1, node2);
                                if ((node1 != qu.front() && visited[0][node1] == 0)) {
                                    visited[0][node1] = count;
                                    qu.push(node1);
                                }
                                else if ((node2 != qu.front() && visited[0][node2] == 0)) {
                                    visited[0][node2] = count;
                                    qu.push(node2);
                                }
                            }
                        }
                        if (inx != 0) {    // this node is not on the left plane
                            eno = inz *(sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + (inx - 1) * (sys->N_cell_y + 1) + iny;
                            if (sys->markEdge[eno] != 0) {
                                sys->compute_edgelink(eno, node1, node2);
                                if ((node1 != qu.front() && visited[0][node1] == 0)) {
                                    visited[0][node1] = count;
                                    qu.push(node1);
                                }
                                else if ((node2 != qu.front() && visited[0][node2] == 0)) {
                                    visited[0][node2] = count;
                                    qu.push(node2);
                                }
                            }
                        }
                        if (inx != sys->nx - 1) {    // this node is not on the right plane
                            eno = inz *(sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + inx * (sys->N_cell_y + 1) + iny;
                            if (sys->markEdge[eno] != 0) {
                                sys->compute_edgelink(eno, node1, node2);
                                if ((node1 != qu.front() && visited[0][node1] == 0)) {
                                    visited[0][node1] = count;
                                    qu.push(node1);
                                }
                                else if ((node2 != qu.front() && visited[0][node2] == 0)) {
                                    visited[0][node2] = count;
                                    qu.push(node2);
                                }
                            }
                        }
                        if (iny != 0) {    // this node is not on the front plane
                            eno = inz *(sys->N_edge_s + sys->N_edge_v) + inx * sys->N_cell_y + iny - 1;
                            if (sys->markEdge[eno] != 0) {
                                sys->compute_edgelink(eno, node1, node2);
                                if ((node1 != qu.front() && visited[0][node1] == 0)) {
                                    visited[0][node1] = count;
                                    qu.push(node1);
                                }
                                else if ((node2 != qu.front() && visited[0][node2] == 0)) {
                                    visited[0][node2] = count;
                                    qu.push(node2);
                                }
                            }
                        }
                        if (iny != sys->ny - 1) {    // this node is not on the back plane
                            eno = inz *(sys->N_edge_s + sys->N_edge_v) + inx * sys->N_cell_y + iny;
                            if (sys->markEdge[eno] != 0) {
                                sys->compute_edgelink(eno, node1, node2);
                                if ((node1 != qu.front() && visited[0][node1] == 0)) {
                                    visited[0][node1] = count;
                                    qu.push(node1);
                                }
                                else if ((node2 != qu.front() && visited[0][node2] == 0)) {
                                    visited[0][node2] = count;
                                    qu.push(node2);
                                }
                            }
                        }
                        qu.pop();
                    }
                }
            }
        }

#ifdef PRINT_VERBOSE_TIMING
        cout << "Time to find isolated conductors is " << (clock() - *tt) * 1.0 / CLOCKS_PER_SEC << " s" << endl;
        cout << "Number of isolated conductors counted is " << count << endl;
#endif

        /*for (myint indi = 0; indi < sys->N_node; indi++) {
            if (visited[0][indi] != 0) {
                for (myint indj = 0; indj < sys->nodeEdge[indi].size(); indj++) {
                    if (sys->markEdge[sys->nodeEdge[indi][indj].first] != 0 && sys->cond2condIn[visited[0][indi] - 1].find(sys->markEdge[sys->nodeEdge[indi][indj].first]) == sys->cond2condIn[visited[0][indi] - 1].end()) {
                        sys->cond2condIn[visited[0][indi] - 1].insert(sys->markEdge[sys->nodeEdge[indi][indj].first]);
                    }
                }
            }
        }*/
        for (myint indi = 0; indi < sys->N_node; indi++) {
            sys->markNode[indi] = visited[0][indi];
        }
        sys->numCdt = count;
    });
    taskMM13.name("BFS for Disjoint Conductors");
    taskMM10.precede(taskMM13);
    taskMM11.precede(taskMM13);
    taskMM12.precede(taskMM13);

    /* Construct each isolated conductor */
    tf::Task taskMM14 = subflow.emplace([=]() {
        /*for (myint indi = 0; indi < sys->numCdt; indi++) {
            cout << "Cnt " << indi << " has condIn as: ";
            for (auto ci : sys->cond2condIn[indi]) {
                cout << ci << " ";
            }
            cout << endl;
        }*/
        sys->conductor = (fdtdCdt*)malloc(sys->numCdt * sizeof(fdtdCdt));
        sys->cdtNumNode = (myint*)calloc(sys->numCdt, sizeof(myint));
        for (myint indi = 0; indi < sys->N_node; indi++) {
            if (visited[0][indi] != 0) {
                sys->cdtNumNode[visited[0][indi] - 1]++;
            }
        }
        for (myint indi = 0; indi < sys->numCdt; indi++) {
            sys->conductor[indi].node = (myint*)calloc(sys->cdtNumNode[indi], sizeof(myint));
            sys->conductor[indi].cdtNodeind = 0; // Initialize to zero
            sys->conductor[indi].markPort = 0;
        }
        myint portground_count = 0;
        for (myint indi = 0; indi < sys->N_node; indi++) {
            if (visited[0][indi] != 0) {
                sys->conductor[visited[0][indi] - 1].node[sys->conductor[visited[0][indi] - 1].cdtNodeind] = indi;
    #ifdef LOWER_BOUNDARY_PEC
                if ((indi < sys->N_node_s) && sys->conductor[visited[0][indi] - 1].markPort != -2 && sys->conductor[visited[0][indi] - 1].markPort != -1) {
                    sys->conductor[visited[0][indi] - 1].markPort = -1;    // this conductor connects to the lower PEC
                }
    #endif
    #ifdef UPPER_BOUNDARY_PEC
                if ((indi >= sys->N_node - sys->N_node_s) && sys->conductor[visited[0][indi] - 1].markPort != -2 && sys->conductor[visited[0][indi] - 1].markPort != -1) {
                    sys->conductor[visited[0][indi] - 1].markPort = -2;    // this conductor connects to the upper PEC
                }
    #endif
                sys->conductor[visited[0][indi] - 1].cdtNodeind++;
            }
        }
    });
    taskMM14.name("Construct Isolated Conductors");
    taskMM13.precede(taskMM14);

    /* Establish relationship between isolated conductors and ports */
    tf::Task taskMM15 = subflow.emplace([=]() {
        bool isSetEmpty = false;
        if (sys->cond2condIn.empty()) {
            isSetEmpty = true; // Change value to prevent checks against sys->cond2condIn.end()
        }
        myint indPortNode1, indPortNode2;
        set<int> cond;    // the port conductors
        myint eno = (myint)0;
        for (myint indi = 0; indi < sys->numPorts; indi++) {
#ifdef PRINT_PORT_COND
            cout << " Checking port" << indi + 1 << endl;
#endif
            int mult = sys->portCoor[indi].multiplicity;
            vector<double> x1coord = sys->portCoor[indi].x1;
            vector<double> y1coord = sys->portCoor[indi].y1;
            vector<double> z1coord = sys->portCoor[indi].z1;
            vector<double> x2coord = sys->portCoor[indi].x2;
            vector<double> y2coord = sys->portCoor[indi].y2;
            vector<double> z2coord = sys->portCoor[indi].z2;
            for (myint indk = 0; indk < mult; indk++) {
                indPortNode1 = sys->markNode[zi->at(z1coord[indk]) * sys->N_node_s + xi->at(x1coord[indk]) * (sys->N_cell_y + 1) + yi->at(y1coord[indk])];
                indPortNode2 = sys->markNode[zi->at(z2coord[indk]) * sys->N_node_s + xi->at(x2coord[indk]) * (sys->N_cell_y + 1) + yi->at(y2coord[indk])];
#ifdef PRINT_PORT_COND
                cout << "  " << zi->at(z1coord[indk]) << " " << xi->at(x1coord[indk]) << " " << yi->at(y1coord[indk]) << endl;
                cout << "  " << zi->at(z2coord[indk]) << " " << xi->at(x2coord[indk]) << " " << yi->at(y2coord[indk]) << endl;
                /*cout << "  " << sys->markEdge[zi->at(z1coord[indk]) * (sys->N_edge_s + sys->N_edge_v) - sys->N_edge_v + xi->at(x1coord[indk]) * (sys->N_cell_y + 1) + yi->at(y1coord[indk])] << endl;*/
                cout << "  mult = " << mult << ", indPortNode1 = " << indPortNode1 << ", indPortNode2 = " << indPortNode2 << endl;
                /*cout << "  First z-coordinate is " << z1coord[indk] << " and second z-coordinate is " << z2coord[indk] << endl;
                cout << "  x-coordinate is " << x1coord[indk] << " and  y-coordinate is " << y1coord[indk] << endl;*/
#endif
                if (indPortNode1 == 0 || indPortNode2 == 0) {
                    cerr << "Port node could not be located within a conductor. Exiting now." << endl;
                    exit(1);
                }

                if (cond.find(indPortNode1) == cond.end()) {
                    cond.insert(indPortNode1);
                    for (myint indj = 0; indj < sys->cdtNumNode[indPortNode1 - 1]; indj++) {
#ifdef PRINT_PORT_COND
                        //cout << "  Checking indPortNode1 against conductor " << indj << " out of " << sys->cdtNumNode[indPortNode1 - 1] << endl;
                        //cout << "  Value of sys->conductor[indPortNode1 - 1].node[indj] = " << sys->conductor[indPortNode1 - 1].node[indj] << endl;
#endif
                        myint inz = sys->conductor[indPortNode1 - 1].node[indj] / sys->N_node_s;
                        myint inx = ((sys->conductor[indPortNode1 - 1].node[indj]) % sys->N_node_s) / (sys->N_cell_y + 1);
                        myint iny = ((sys->conductor[indPortNode1 - 1].node[indj]) % sys->N_node_s) % (sys->N_cell_y + 1);
#ifdef PRINT_PORT_COND
                        //cout << "   inz = " << inz << ", inx = " << inx << ", iny = " << iny << endl;
#endif
                        if (inz != 0) {    // this node is not on the bottom plane
                            eno = (inz - 1) * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx * (sys->N_cell_y + 1) + iny;    // the node's lower edge
                            if (sys->markEdge[eno] != 0 && isSetEmpty) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                                isSetEmpty = false;
                            }
                            else if (sys->markEdge[eno] != 0 && sys->cond2condIn.find(sys->markEdge[eno]) == sys->cond2condIn.end()) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                            }
                        }
                        if (inz != sys->nz - 1) {    // this node is not on the upper plane
                            eno = inz * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx * (sys->N_cell_y + 1) + iny;    // the node's lower edge
                            if (sys->markEdge[eno] != 0 && isSetEmpty) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                                isSetEmpty = false;
                            }
                            else if (sys->markEdge[eno] != 0 && sys->cond2condIn.find(sys->markEdge[eno]) == sys->cond2condIn.end()) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                            }
                        }
                        if (inx != 0) {    // this node is not on the left plane
                            eno = inz *(sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + (inx - 1) * (sys->N_cell_y + 1) + iny;
                            if (sys->markEdge[eno] != 0 && isSetEmpty) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                                isSetEmpty = false;
                            }
                            else if (sys->markEdge[eno] != 0 && sys->cond2condIn.find(sys->markEdge[eno]) == sys->cond2condIn.end()) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                            }
                        }
                        if (inx != sys->nx - 1) {    // this node is not on the right plane
                            eno = inz *(sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + inx * (sys->N_cell_y + 1) + iny;
                            if (sys->markEdge[eno] != 0 && isSetEmpty) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                                isSetEmpty = false;
                            }
                            else if (sys->markEdge[eno] != 0 && sys->cond2condIn.find(sys->markEdge[eno]) == sys->cond2condIn.end()) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                            }
                        }
                        if (iny != 0) {    // this node is not on the front plane
                            eno = inz *(sys->N_edge_s + sys->N_edge_v) + inx * sys->N_cell_y + iny - 1;
                            if (sys->markEdge[eno] != 0 && isSetEmpty) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                                isSetEmpty = false;
                            }
                            else if (sys->markEdge[eno] != 0 && sys->cond2condIn.find(sys->markEdge[eno]) == sys->cond2condIn.end()) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                            }
                        }
                        if (iny != sys->ny - 1) {    // this node is not on the back plane
                            eno = inz *(sys->N_edge_s + sys->N_edge_v) + inx * sys->N_cell_y + iny;
                            if (sys->markEdge[eno] != 0 && isSetEmpty) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                                isSetEmpty = false;
                            }
                            else if (sys->markEdge[eno] != 0 && sys->cond2condIn.find(sys->markEdge[eno]) == sys->cond2condIn.end()) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                            }
                        }
                    }
                }
#ifdef PRINT_PORT_COND
                //cout << "  Checked indPortNode1 against final conductor, and now checking indPortNode2" << endl;
#endif
                if (cond.find(indPortNode2) == cond.end()) {
                    cond.insert(indPortNode2);
                    for (myint indj = 0; indj < sys->cdtNumNode[indPortNode2 - 1]; indj++) {
#ifdef PRINT_PORT_COND
                        //cout << "  Checking indPortNode2 against conductor " << indj << " out of " << sys->cdtNumNode[indPortNode2 - 1] << endl;
                        //cout << "  Value of sys->conductor[indPortNode2 - 1].node[indj] = " << sys->conductor[indPortNode2 - 1].node[indj] << endl;
#endif
                        myint inz = sys->conductor[indPortNode2 - 1].node[indj] / sys->N_node_s;
                        myint inx = ((sys->conductor[indPortNode2 - 1].node[indj]) % sys->N_node_s) / (sys->N_cell_y + 1);
                        myint iny = ((sys->conductor[indPortNode2 - 1].node[indj]) % sys->N_node_s) % (sys->N_cell_y + 1);
#ifdef PRINT_PORT_COND
                        //cout << "   inz = " << inz << ", inx = " << inx << ", iny = " << iny << endl;
#endif
                        if (inz != 0) {    // this node is not on the bottom plane
                            eno = (inz - 1) * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx * (sys->N_cell_y + 1) + iny;    // the node's lower edge
                            if (sys->markEdge[eno] != 0 && isSetEmpty) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                                isSetEmpty = false;
                            }
                            else if (sys->markEdge[eno] != 0 && sys->cond2condIn.find(sys->markEdge[eno]) == sys->cond2condIn.end()) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                            }
                        }
                        if (inz != sys->nz - 1) {    // this node is not on the upper plane
                            eno = inz * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx * (sys->N_cell_y + 1) + iny;    // the node's lower edge
                            if (sys->markEdge[eno] != 0 && isSetEmpty) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                                isSetEmpty = false;
                            }
                            else if (sys->markEdge[eno] != 0 && sys->cond2condIn.find(sys->markEdge[eno]) == sys->cond2condIn.end()) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                            }
                        }
                        if (inx != 0) {    // this node is not on the left plane
                            eno = inz *(sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + (inx - 1) * (sys->N_cell_y + 1) + iny;
                            if (sys->markEdge[eno] != 0 && isSetEmpty) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                                isSetEmpty = false;
                            }
                            else if (sys->markEdge[eno] != 0 && sys->cond2condIn.find(sys->markEdge[eno]) == sys->cond2condIn.end()) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                            }
                        }
                        if (inx != sys->nx - 1) {    // this node is not on the right plane
                            eno = inz *(sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + inx * (sys->N_cell_y + 1) + iny;
                            if (sys->markEdge[eno] != 0 && isSetEmpty) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                                isSetEmpty = false;
                            }
                            else if (sys->markEdge[eno] != 0 && sys->cond2condIn.find(sys->markEdge[eno]) == sys->cond2condIn.end()) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                            }
                        }
                        if (iny != 0) {    // this node is not on the front plane
                            eno = inz *(sys->N_edge_s + sys->N_edge_v) + inx * sys->N_cell_y + iny - 1;
                            if (sys->markEdge[eno] != 0 && isSetEmpty) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                                isSetEmpty = false;
                            }
                            else if (sys->markEdge[eno] != 0 && sys->cond2condIn.find(sys->markEdge[eno]) == sys->cond2condIn.end()) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                            }
                        }
                        if (iny != sys->ny - 1) {    // this node is not on the back plane
                            eno = inz *(sys->N_edge_s + sys->N_edge_v) + inx * sys->N_cell_y + iny;
                            if (sys->markEdge[eno] != 0 && isSetEmpty) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                                isSetEmpty = false;
                            }
                            else if (sys->markEdge[eno] != 0 && sys->cond2condIn.find(sys->markEdge[eno]) == sys->cond2condIn.end()) {
                                sys->cond2condIn.insert(sys->markEdge[eno]);
                            }
                        }
                    }
                }
            }
        }
        cond.clear();
        cout << "cond2condIn is set sucessfully!" << endl;
    });
    taskMM15.name("Set cond2condIn for Ports");
    taskMM14.precede(taskMM15);


    /* Find the two nodes defining each edge (seems odd not to have this already) */
    tf::Task taskMM16 = subflow.emplace([=]() {
        sys->outedge = 0;
        sys->inedge = 0;
        *tt = clock();
        myint node1 = (myint)0;
        myint node2 = (myint)0;
        for (myint indi = 0; indi < sys->N_edge; indi++) {
            // Find edge indi's two nodes
            sys->compute_edgelink(indi, node1, node2);
            if (sys->markEdge[indi] != 0 && visited[0][node1] == visited[0][node2] && visited[0][node1] != 0) {
                sys->markEdge[indi] = visited[0][node2];    // Mark the edge with each color for different conductors

            }
            if (sys->markEdge[indi] != 0) {
                sys->inedge++;
            }
            else {
                sys->outedge++;
            }
        }

#ifdef PRINT_VERBOSE_TIMING
        cout << "Time to assign markEdge is " << (clock() - *tt) * 1.0 / CLOCKS_PER_SEC << " s" << endl;
        cout << "The number of outside conductor edges is " << sys->outedge << endl;
        cout << "The number of inside conductor edges is " << sys->inedge << endl;
#endif

        free(visited[0]);
    });
    taskMM16.name("Find Edge Nodes");
    taskMM15.precede(taskMM16);

    /* Set markCell */
    tf::Task taskMM17 = subflow.emplace([=]() {
#ifndef SKIP_MARK_CELL
        vector<int> aa;
        vector<double> bb;
        sys->markCell = (myint*)calloc(sys->N_cell_x * sys->N_cell_y * sys->N_cell_z, sizeof(myint));
        for (myint indi = 0; indi < sys->N_edge; indi++)
        {
            sys->edgeCell.push_back(aa);
            sys->edgeCellArea.push_back(bb);
        }
        myint cell;
        for (myint indi = 0; indi < sys->N_cell_z; indi++) {
            for (myint indj = 0; indj < sys->N_cell_x; indj++) {
                for (k = 0; k < sys->N_cell_y; k++) {
                    myint mark = (myint)1;
                    cell = indi * sys->N_patch_s + indj * sys->N_cell_y + k;
                    myint count = sys->markEdge[indi * (sys->N_edge_s + sys->N_edge_v) + indj * (sys->N_cell_y) + k];

                    // y axis
                    sys->edgeCell[(indi * (sys->N_edge_s + sys->N_edge_v) + indj * (sys->N_cell_y) + k)].push_back(cell);
                    sys->edgeCellArea[(indi * (sys->N_edge_s + sys->N_edge_v) + indj * (sys->N_cell_y) + k)].push_back((sys->xn[indj + 1] - sys->xn[indj]) * (sys->zn[indi + 1] - sys->zn[indi]));
                    if (sys->markEdge[indi * (sys->N_edge_s + sys->N_edge_v) + indj * (sys->N_cell_y) + k] == 0) {
                        mark = 0;
                    }

                    sys->edgeCell[indi * (sys->N_edge_s + sys->N_edge_v) + (indj + 1) * (sys->N_cell_y) + k].push_back(cell);
                    sys->edgeCellArea[indi * (sys->N_edge_s + sys->N_edge_v) + (indj + 1) * (sys->N_cell_y) + k].push_back((sys->xn[indj + 1] - sys->xn[indj]) * (sys->zn[indi + 1] - sys->zn[indi]));
                    if (sys->markEdge[indi * (sys->N_edge_s + sys->N_edge_v) + (indj + 1) * (sys->N_cell_y) + k] == 0 || sys->markEdge[indi * (sys->N_edge_s + sys->N_edge_v) + (indj + 1) * (sys->N_cell_y) + k] != count) {
                        mark = 0;
                    }

                    sys->edgeCell[(indi + 1) * (sys->N_edge_s + sys->N_edge_v) + indj * (sys->N_cell_y) + k].push_back(cell);
                    sys->edgeCellArea[(indi + 1) * (sys->N_edge_s + sys->N_edge_v) + indj * (sys->N_cell_y) + k].push_back((sys->xn[indj + 1] - sys->xn[indj]) * (sys->zn[indi + 1] - sys->zn[indi]));
                    if (sys->markEdge[(indi + 1) * (sys->N_edge_s + sys->N_edge_v) + indj * (sys->N_cell_y) + k] == 0 || sys->markEdge[(indi + 1) * (sys->N_edge_s + sys->N_edge_v) + indj * (sys->N_cell_y) + k] != count) {
                        mark = 0;
                    }

                    sys->edgeCell[(indi + 1) * (sys->N_edge_s + sys->N_edge_v) + (indj + 1) * (sys->N_cell_y) + k].push_back(cell);
                    sys->edgeCellArea[(indi + 1) * (sys->N_edge_s + sys->N_edge_v) + (indj + 1) * (sys->N_cell_y) + k].push_back((sys->xn[indj + 1] - sys->xn[indj]) * (sys->zn[indi + 1] - sys->zn[indi]));
                    if (sys->markEdge[(indi + 1) * (sys->N_edge_s + sys->N_edge_v) + (indj + 1) * (sys->N_cell_y) + k] == 0 || sys->markEdge[(indi + 1) * (sys->N_edge_s + sys->N_edge_v) + (indj + 1) * (sys->N_cell_y) + k] != count) {
                        mark = 0;
                    }

                    // x axis
                    sys->edgeCell[indi * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y) * (sys->N_cell_x + 1) + indj * (sys->N_cell_y + 1) + k].push_back(cell);
                    sys->edgeCellArea[indi * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y) * (sys->N_cell_x + 1) + indj * (sys->N_cell_y + 1) + k].push_back((sys->yn[k + 1] - sys->yn[k]) * (sys->zn[indi + 1] - sys->zn[indi]));
                    if (sys->markEdge[indi * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y) * (sys->N_cell_x + 1) + indj * (sys->N_cell_y + 1) + k] == 0 || sys->markEdge[indi * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y) * (sys->N_cell_x + 1) + indj * (sys->N_cell_y + 1) + k] != count) {
                        mark = 0;
                    }

                    sys->edgeCell[indi * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y) * (sys->N_cell_x + 1) + indj * (sys->N_cell_y + 1) + k + 1].push_back(cell);
                    sys->edgeCellArea[indi * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y) * (sys->N_cell_x + 1) + indj * (sys->N_cell_y + 1) + k + 1].push_back((sys->yn[k + 1] - sys->yn[k])*(sys->zn[indi + 1] - sys->zn[indi]));
                    if (sys->markEdge[indi * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y) * (sys->N_cell_x + 1) + indj * (sys->N_cell_y + 1) + k + 1] == 0 || sys->markEdge[indi * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y) * (sys->N_cell_x + 1) + indj * (sys->N_cell_y + 1) + k + 1] != count) {
                        mark = 0;
                    }

                    sys->edgeCell[(indi + 1) * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y) * (sys->N_cell_x + 1) + indj * (sys->N_cell_y + 1) + k].push_back(cell);
                    sys->edgeCellArea[(indi + 1) * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y) * (sys->N_cell_x + 1) + indj * (sys->N_cell_y + 1) + k].push_back((sys->yn[k + 1] - sys->yn[k])*(sys->zn[indi + 1] - sys->zn[indi]));
                    if (sys->markEdge[(indi + 1) * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y) * (sys->N_cell_x + 1) + indj * (sys->N_cell_y + 1) + k] == 0 || sys->markEdge[(indi + 1) * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y) * (sys->N_cell_x + 1) + indj * (sys->N_cell_y + 1) + k] != count) {
                        mark = 0;
                    }

                    sys->edgeCell[(indi + 1) * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y) * (sys->N_cell_x + 1) + indj * (sys->N_cell_y + 1) + k + 1].push_back(cell);
                    sys->edgeCellArea[(indi + 1) * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y) * (sys->N_cell_x + 1) + indj * (sys->N_cell_y + 1) + k + 1].push_back((sys->yn[k + 1] - sys->yn[k]) * (sys->zn[indi + 1] - sys->zn[indi]));
                    if (sys->markEdge[(indi + 1) * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y) * (sys->N_cell_x + 1) + indj * (sys->N_cell_y + 1) + k + 1] == 0 || sys->markEdge[(indi + 1) * (sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y) * (sys->N_cell_x + 1) + indj * (sys->N_cell_y + 1) + k + 1] != count) {
                        mark = 0;
                    }

                    // z axis
                    sys->edgeCell[indi * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indj *(sys->N_cell_y + 1) + k].push_back(cell);
                    sys->edgeCellArea[indi * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indj *(sys->N_cell_y + 1) + k].push_back((sys->xn[indj + 1] - sys->xn[indj]) * (sys->yn[k + 1] - sys->yn[k]));
                    if (sys->markEdge[indi * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indj *(sys->N_cell_y + 1) + k] == 0 || sys->markEdge[indi * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indj *(sys->N_cell_y + 1) + k] != count) {
                        mark = 0;
                    }

                    sys->edgeCell[indi * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indj *(sys->N_cell_y + 1) + k + 1].push_back(cell);
                    sys->edgeCellArea[indi * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indj *(sys->N_cell_y + 1) + k + 1].push_back((sys->xn[indj + 1] - sys->xn[indj]) * (sys->yn[k + 1] - sys->yn[k]));
                    if (sys->markEdge[indi * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indj *(sys->N_cell_y + 1) + k + 1] == 0 || sys->markEdge[indi * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indj *(sys->N_cell_y + 1) + k + 1] != count) {
                        mark = 0;
                    }

                    sys->edgeCell[indi * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + (indj + 1) *(sys->N_cell_y + 1) + k].push_back(cell);
                    sys->edgeCellArea[indi * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + (indj + 1) *(sys->N_cell_y + 1) + k].push_back((sys->xn[indj + 1] - sys->xn[indj]) * (sys->yn[k + 1] - sys->yn[k]));
                    if (sys->markEdge[indi * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + (indj + 1) *(sys->N_cell_y + 1) + k] == 0 || sys->markEdge[indi * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + (indj + 1) *(sys->N_cell_y + 1) + k] != count) {
                        mark = 0;
                    }

                    sys->edgeCell[indi * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + (indj + 1) *(sys->N_cell_y + 1) + k + 1].push_back(cell);
                    sys->edgeCellArea[indi * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + (indj + 1) *(sys->N_cell_y + 1) + k + 1].push_back((sys->xn[indj + 1] - sys->xn[indj]) * (sys->yn[k + 1] - sys->yn[k]));
                    if (sys->markEdge[indi * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + (indj + 1) *(sys->N_cell_y + 1) + k + 1] == 0 || sys->markEdge[indi * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + (indj + 1) *(sys->N_cell_y + 1) + k + 1] != count) {
                        mark = 0;
                    }

                    if (mark == 1) {
                        sys->markCell[cell] = (myint)1;
                    }
                }
            }
        }
#endif

        /* Return maps */
        //xi1.insert_or_assign(4.5, 2);
        //xi1 = *xi;
        //yi1 = *yi;
        //zi1 = *zi;

        /* Deallocate memory */
        delete xmin;
        delete xmax;
        delete ymin;
        delete ymax;
        delete numNode;
        delete numPortSides;
        delete numOrigOldXY;
        delete numOrigOldZ;
        delete[] xOrigOld;
        delete[] yOrigOld;
        delete[] zOrigOld;
        delete disMinz;
        delete disMaxx;
        delete disMaxy;
        delete minLayerDist;
        delete tt;
        delete[] visited;
        cout << "meshAndMark dynamic execution time is " << (clock() - t0) * 1.0 / CLOCKS_PER_SEC << " s" << endl << endl;
    });
    taskMM17.name("Mark Cells and Return Maps");
    taskMM16.precede(taskMM17);
    return 0;
}

int matrixConstruction(fdtdMesh *sys) {
    int bdl = 0, bdu = 0;
#ifdef LOWER_BOUNDARY_PEC
    bdl = 1;
#endif
#ifdef UPPER_BOUNDARY_PEC
    bdu = 1;
#endif
    myint indi = 0, indj = 0;
    //cout << sys->stackEpsn.size() << endl;
    /* construct D_eps */
    /*sys->eps.reserve(sys->N_edge);
    for (myint indi = 0; indi < sys->N_edge; indi++) {
        if (indi < sys->N_edge_s) {
            sys->eps.at(indi) = sys->stackEpsn[0] * EPSILON0;
        }
        else {
            sys->eps.at(indi) = sys->stackEpsn[(indi - sys->N_edge_s) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
        }
    }*/

    /* construct D_sig */
    /*sys->sig = (double*)calloc(sys->N_edge, sizeof(double));
    double area, b;
    for (myint indi = 0; indi < sys->N_edge; indi++) {
        if (sys->markEdge[indi] != 0) {
#ifndef SKIP_MARK_CELL
            area = 0;
            b = 0;
            for (myint indj = 0; indj < sys->edgeCell[indi].size(); indj++) {
                area += (double)sys->markCell[sys->edgeCell[indi][indj]] * sys->edgeCellArea[indi][indj];
                b += sys->edgeCellArea[indi][indj];
            }
            sys->sig[indi] = area / b * SIGMA;
#else
            sys->sig[indi] = SIGMA;
#endif
        }
    }*/
    /*ofstream out;
    out.open("eps.txt", std::ofstream::out | std::ofstream::trunc);
    for (myint indi = bdl * sys->N_edge_s; indi < sys->N_edge - bdu * sys->N_edge_s; indi++){
        out << sys->stackEpsn[(indi + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0 << endl;
    }
    out.close();

    out.open("sig.txt", std::ofstream::out | std::ofstream::trunc);
    for (myint indi = bdl * sys->N_edge_s; indi < sys->N_edge - bdu * sys->N_edge_s; indi++){
        if (sys->markEdge[indi] != 0){
            out << SIGMA << endl;
        }
        else{
            out << 0 << endl;
        }
    }
    out.close();*/

    sys->edgeCell.clear();
    sys->edgeCell.shrink_to_fit();
    sys->edgeCellArea.clear();
    sys->edgeCellArea.shrink_to_fit();
    //free(sys->markCell); sys->markCell = NULL;

    /*sys->stackEpsn.clear();*/
    return 0;
}

int portSet(fdtdMesh* sys, unordered_map<double, int> xi, unordered_map<double, int> yi, unordered_map<double, int> zi, tf::Subflow subflow) {
    clock_t t0 = clock();
    tf::Task taskPS0 = subflow.placeholder();
    double sideLen = 0.;
    taskPS0.name("Initialize Vars for portSet()");

    auto[PS1Start, PS1End] = subflow.parallel_for(0, sys->numPorts, 1, [=](myint indi)
    {
        /* Send error if port unretrievable */
        cout << " Setting port #" << indi + 1 << endl;
        int mult = sys->portCoor[indi].multiplicity;
        if (mult == 0)
        {
            cerr << "Unable to access port #" << indi + 1 << ". Exiting now." << endl;
            exit(1);
        }

        /* Mark nodes and conductors corresponding to each port side */
        vector<double> x1coord = sys->portCoor[indi].x1;
        vector<double> y1coord = sys->portCoor[indi].y1;
        vector<double> z1coord = sys->portCoor[indi].z1;
        vector<double> x2coord = sys->portCoor[indi].x2;
        vector<double> y2coord = sys->portCoor[indi].y2;
        vector<double> z2coord = sys->portCoor[indi].z2;
        sys->portCoor[indi].portEdge.reserve(mult); // Reserve enough port edge vectors themselves to match number of port sides
        for (myint indk = 0; indk < mult; indk++)
        {
            /* Extract the node markers */
            myint indMarkNode1 = sys->markNode[zi.at(z1coord[indk]) * sys->N_node_s + xi.at(x1coord[indk]) * (sys->N_cell_y + 1) + yi.at(y1coord[indk])];
            myint indMarkNode2 = sys->markNode[zi.at(z2coord[indk]) * sys->N_node_s + xi.at(x2coord[indk]) * (sys->N_cell_y + 1) + yi.at(y2coord[indk])];

            /* Track conductor index each port side touches and mark each conductor with the port number */
            if ((indMarkNode1 != 0) && (sys->conductor[indMarkNode1 - 1].markPort > -1)) { // Only supply end of port not on PEC
                sys->portCoor[indi].portCnd.push_back(indMarkNode1);
                sys->conductor[indMarkNode1 - 1].markPort = indi + 1;    // markPort index starts from 1
            }
            else if ((indMarkNode2 != 0) && (sys->conductor[indMarkNode2 - 1].markPort > -1)) { // Only return end of port not on PEC
                sys->portCoor[indi].portCnd.push_back(indMarkNode2);
                sys->conductor[indMarkNode2 - 1].markPort = indi + 1;    // markPort index starts from 1
            }
            else if (indMarkNode1 != 0 && indMarkNode2 != 0) {
                if (sys->conductor[indMarkNode1 - 1].markPort == -2) {    // the upper PEC conductor is excited
                    sys->portCoor[indi].portCnd.push_back(indMarkNode1);
                }
                else if (sys->conductor[indMarkNode2 - 1].markPort == -2) {
                    sys->portCoor[indi].portCnd.push_back(indMarkNode2);
                }
                //sys->portCoor[indi].portCnd.push_back(indMarkNode2);
                /*sys->conductor[indMarkNode1 - 1].markPort = indi + 1;
                sys->conductor[indMarkNode2 - 1].markPort = indi + 1;*/
            }
            /*for (myint indj = 0; indj < sys->cdtNumNode[sys->portCoor[indi].portCnd.back() - 1]; indj++) {
            sys->exciteCdtLayer[sys->conductor[sys->portCoor[indi].portCnd.back() - 1].node[indj] / sys->N_node_s] = 1;
            }*/
#ifdef PRINT_PORT_SET
            cout << "portSet() logic test: markNode on Point 1 (" << indMarkNode1 << "), port marker on corresponding isolated conductor for Point 1 (" << sys->conductor[indMarkNode1 - 1].markPort << "), markNode on Point 2 (" << indMarkNode2 << "), port marker on corresponding isolated conductor for Point 2 (" << sys->conductor[indMarkNode2 - 1].markPort << ")" << endl;
            cout << "Value of portCnd for port #" << indi + 1 << " (port side " << indk << "): " << sys->portCoor[indi].portCnd.back() << endl;
#endif
            /* Explicitly identify edges belonging to the port side and calculate cross-sectional area for port side current density */
            /* Assumption: port supply and return points differ in only one Cartesian coordinate */
            vector<myint> edge;
            if (x1coord[indk] != x2coord[indk]) {
                if (x1coord[indk] < x2coord[indk]) {
                    for (myint indj = xi.at(x1coord[indk]); indj < xi.at(x2coord[indk]); indj++) {
                        edge.push_back((sys->N_edge_s + sys->N_edge_v)*zi.at(z1coord[indk]) + (sys->N_cell_y)*(sys->N_cell_x + 1) + indj*(sys->N_cell_y + 1) + yi.at(y1coord[indk]));
                        //cout << " port #" << indi + 1 << ", side #" << indk + 1 << ", edge #" << indj << " has edge index #" << edge.back() << endl;
                    }
                }
                else {
                    for (myint indj = xi.at(x2coord[indk]); indj < xi.at(x1coord[indk]); indj++) {
                        edge.push_back((sys->N_edge_s + sys->N_edge_v)*zi.at(z1coord[indk]) + (sys->N_cell_y)*(sys->N_cell_x + 1) + indj*(sys->N_cell_y + 1) + yi.at(y1coord[indk]));
                        //cout << " port #" << indi + 1 << ", side #" << indk + 1 << ", edge #" << indj << " has edge index #" << edge.back() << endl;
                    }
                }

                double area = 1.;
                if (yi.at(y1coord[indk]) == 0) {
                    area *= (sys->yn[yi.at(y1coord[indk]) + 1] - y1coord[indk]);
                }
                else if (yi.at(y1coord[indk]) == sys->N_cell_y) {
                    area *= (y1coord[indk] - sys->yn[yi.at(y1coord[indk]) - 1]);
                }
                else {
                    area *= ((sys->yn[yi.at(y1coord[indk]) + 1] - y1coord[indk]) / 2 + (y1coord[indk] - sys->yn[yi.at(y1coord[indk]) - 1]) / 2);
                }
                if (zi.at(z1coord[indk]) == 0) {
                    area *= (sys->zn[zi.at(z1coord[indk]) + 1] - z1coord[indk]);
                }
                else if (zi.at(z1coord[indk]) == sys->N_cell_z) {
                    area *= (z1coord[indk] - sys->zn[zi.at(z1coord[indk]) - 1]);
                }
                else {
                    area *= ((sys->zn[zi.at(z1coord[indk]) + 1] - z1coord[indk]) / 2 + (z1coord[indk] - sys->zn[zi.at(z1coord[indk]) - 1]) / 2);
                }

                /* Save port area and port edges */
                sys->portCoor[indi].portArea.push_back(area);
                sys->portCoor[indi].portEdge.push_back(edge);
            }
            else if (y1coord[indk] != y2coord[indk]) {
                if (y1coord[indk] < y2coord[indk]) {
                    for (myint indj = yi.at(y1coord[indk]); indj < yi.at(y2coord[indk]); indj++) {
                        edge.push_back((sys->N_edge_s + sys->N_edge_v)*zi.at(z1coord[indk]) + (sys->N_cell_y)*xi.at(x1coord[indk]) + indj);
                    }
                }
                else {
                    for (myint indj = yi.at(y2coord[indk]); indj < yi.at(y1coord[indk]); indj++) {
                        edge.push_back((sys->N_edge_s + sys->N_edge_v)*zi.at(z1coord[indk]) + (sys->N_cell_y)*xi.at(x1coord[indk]) + indj);
                    }
                }

                double area = 1.;
                if (xi.at(x1coord[indk]) == 0) {
                    area *= (sys->xn[xi.at(x1coord[indk]) + 1] - x1coord[indk]);
                }
                else if (xi.at(x1coord[indk]) == sys->N_cell_x) {
                    area *= (x1coord[indk] - sys->xn[xi.at(x1coord[indk]) - 1]);
                }
                else {
                    area *= ((sys->xn[xi.at(x1coord[indk]) + 1] - x1coord[indk]) / 2 + (x1coord[indk] - sys->xn[xi.at(x1coord[indk]) - 1]) / 2);
                }
                if (zi.at(z1coord[indk]) == 0) {
                    area *= (sys->zn[zi.at(z1coord[indk]) + 1] - z1coord[indk]);
                }
                else if (zi.at(z1coord[indk]) == sys->N_cell_z) {
                    area *= (z1coord[indk] - sys->zn[zi.at(z1coord[indk]) - 1]);
                }
                else {
                    area *= ((sys->zn[zi.at(z1coord[indk]) + 1] - z1coord[indk]) / 2 + (z1coord[indk] - sys->zn[zi.at(z1coord[indk]) - 1]) / 2);
                }

                /* Save port area and port edges */
                sys->portCoor[indi].portArea.push_back(area);
                sys->portCoor[indi].portEdge.push_back(edge);
            }
            else if (z1coord[indk] != z2coord[indk]) {
                if (z1coord[indk] < z2coord[indk]) {
                    for (myint indj = zi.at(z1coord[indk]); indj < zi.at(z2coord[indk]); indj++) {
                        edge.push_back((sys->N_edge_s + sys->N_edge_v)*indj + sys->N_edge_s + (sys->N_cell_y + 1)*xi.at(x1coord[indk]) + yi.at(y1coord[indk]));
                    }
                }
                else {
                    for (myint indj = zi.at(z2coord[indk]); indj < zi.at(z1coord[indk]); indj++) {
                        edge.push_back((sys->N_edge_s + sys->N_edge_v)*indj + sys->N_edge_s + (sys->N_cell_y + 1)*xi.at(x1coord[indk]) + yi.at(y1coord[indk]));
                    }
                }

                double area = 1.;
                if (xi.at(x1coord[indk]) == 0) {
                    area *= (sys->xn[xi.at(x1coord[indk]) + 1] - x1coord[indk]);
                }
                else if (xi.at(x1coord[indk]) == sys->N_cell_x) {
                    area *= (x1coord[indk] - sys->xn[xi.at(x1coord[indk]) - 1]);
                }
                else {
                    area *= ((sys->xn[xi.at(x1coord[indk]) + 1] - x1coord[indk]) / 2 + (x1coord[indk] - sys->xn[xi.at(x1coord[indk]) - 1]) / 2);
                }
                if (yi.at(y1coord[indk]) == 0) {
                    area *= (sys->yn[yi.at(y1coord[indk]) + 1] - y1coord[indk]);
                }
                else if (yi.at(y1coord[indk]) == sys->N_cell_y) {
                    area *= (y1coord[indk] - sys->yn[yi.at(y1coord[indk]) - 1]);
                }
                else {
                    area *= ((sys->yn[yi.at(y1coord[indk]) + 1] - y1coord[indk]) / 2 + (y1coord[indk] - sys->yn[yi.at(y1coord[indk]) - 1]) / 2);
                }

                /* Save port area and port edges */
                sys->portCoor[indi].portArea.push_back(area);
                sys->portCoor[indi].portEdge.push_back(edge);
            }
        }
    });
    PS1Start.work([=]() { cout << endl << "Begin to set ports!" << endl; });
    PS1Start.name("Synchronize for Parallel Port Setting");
    PS1End.work([=]() { cout << "All ports set!" << endl; });
    PS1End.name("Finalize Parallel Port Setting");
    taskPS0.precede(PS1Start);

    /* Check whether each port edge goes through any conductor */
    tf::Task taskPS2 = subflow.emplace([=]() {
#ifdef PRINT_PORT_SET
        cout << "Number of ports is " << sys->numPorts << endl;
        for (myint indi = 0; indi < sys->numPorts; indi++) {
            cout << " Port " << indi + 1 << ":  ";
            for (myint indj = 0; indj < sys->portCoor[indi].portEdge[0].size(); indj++) {
                cout << sys->markEdge[sys->portCoor[indi].portEdge[0][indj]] << " ";
            }
            cout << endl;
        }
#endif

        /* Check portArea */
        //cout << endl;
        //for (myint indi = 0; indi < sys->numPorts; indi++){
        //    double sourceCurrent = 0.; // In-phase current from unit source port edge current densities into supply point (A)
        //    for (int sourcePortSide = 0; sourcePortSide < sys->portCoor[indi].multiplicity; sourcePortSide++)
        //    {
        //        sourceCurrent += sys->portCoor[indi].portArea[sourcePortSide];
        //    }
        //    cout << "SourcePort " << indi << "'s area is " << sourceCurrent << endl;
        //}
        //cout << endl;
    });
    taskPS2.name("Check port edges");
    PS1End.precede(taskPS2);

    /* Find markProSide side nodes near ports for less aggressive node merging using crazy index math */
    tf::Task taskPS3 = subflow.emplace([=]() {
        clock_t t1 = clock();
        sys->markProSide.assign(sys->N_node, false); // Start with false in all entries of markProSide
        sys->markProSide.shrink_to_fit();
        double x1 = 0., x2 = 0., y1 = 0., y2 = 0.;
        myint x1_ind = 0, x2_ind = 0, y1_ind = 0, y2_ind = 0, z1_ind = 0, z2_ind = 0;
        myint indl = 0;
        if (sideLen != 0.) {
            for (auto ci : sys->cond2condIn) {
                //cout << "ci = " << ci << " out of " << sys->cond2condIn.size() << " possible in unordered set" << endl;
                for (indl = 0; indl < sys->conductorIn[ci - 1].numVert - 1; indl++) {
                    //cout << "indl = " << indl << " out of " << sys->conductorIn[ci - 1].numVert - 1 << endl;
                    if (sys->conductorIn[ci - 1].x[indl] == sys->conductorIn[ci - 1].x[indl + 1]) {
                        //cout << "Made it to first if statement in auto ci" << endl;
                        x1 = sys->conductorIn[ci - 1].x[indl];
                        x2 = x1;
                        if (sys->conductorIn[ci - 1].y[indl] < sys->conductorIn[ci - 1].y[indl + 1]) {
                            y1 = sys->conductorIn[ci - 1].y[indl];
                            y2 = sys->conductorIn[ci - 1].y[indl + 1];
                        }
                        else {
                            y1 = sys->conductorIn[ci - 1].y[indl + 1];
                            y2 = sys->conductorIn[ci - 1].y[indl];
                        }
                        x1_ind = xi.at(x1);
                        x2_ind = xi.at(x2);
                        y1_ind = yi.at(y1);
                        y2_ind = yi.at(y2);
                        while (x1 - sys->xn[x1_ind] <= sideLen && x1_ind >= 0) {
                            x1_ind--;
                        }
                        x1_ind++;
                        while (sys->xn[x2_ind] - x2 <= sideLen && x2_ind < sys->nx) {
                            x2_ind++;
                        }
                        x2_ind--;
                        while (y1 - sys->yn[y1_ind] <= sideLen && y1_ind >= 0) {
                            y1_ind--;
                        }
                        y1_ind++;
                        while (sys->yn[y2_ind] - y2 <= sideLen && y2_ind < sys->ny) {
                            y2_ind++;
                        }
                        y2_ind--;
                        z1_ind = zi.at(sys->conductorIn[ci - 1].zmin);
                        z2_ind = zi.at(sys->conductorIn[ci - 1].zmax);
                        for (myint indk = z1_ind; indk <= z2_ind; indk++) {
                            for (myint indj = x1_ind; indj <= x2_ind; indj++) {
                                for (myint indm = y1_ind; indm <= y2_ind; indm++) {
                                    if (sys->markNode[indk * sys->N_node_s + indj * (sys->N_cell_y + 1) + indm] == 0) {
                                        sys->markProSide.at(indk * sys->N_node_s + indj * (sys->N_cell_y + 1) + indm) = true; // Safer to use .at() to assign vector entries over initialized values
                                    }
                                }
                            }
                        }
                    }
                    else {
                        //cout << "Made it to first else statement in auto ci" << endl;
                        y1 = sys->conductorIn[ci - 1].y[indl];
                        y2 = y1;
                        if (sys->conductorIn[ci - 1].x[indl] < sys->conductorIn[ci - 1].x[indl + 1]) {
                            x1 = sys->conductorIn[ci - 1].x[indl];
                            x2 = sys->conductorIn[ci - 1].x[indl + 1];
                        }
                        else {
                            x1 = sys->conductorIn[ci - 1].x[indl + 1];
                            x2 = sys->conductorIn[ci - 1].x[indl];
                        }
                        x1_ind = xi.at(x1);
                        x2_ind = xi.at(x2);
                        y1_ind = yi.at(y1);
                        y2_ind = yi.at(y2);
                        while (x1 - sys->xn[x1_ind] <= sideLen && x1_ind >= 0) {
                            x1_ind--;
                        }
                        x1_ind++;
                        while (sys->xn[x2_ind] - x2 <= sideLen && x2_ind < sys->nx) {
                            x2_ind++;
                        }
                        x2_ind--;
                        while (y1 - sys->yn[y1_ind] <= sideLen && y1_ind >= 0) {
                            y1_ind--;
                        }
                        y1_ind++;
                        while (sys->yn[y2_ind] - y2 <= sideLen && y2_ind < sys->ny) {
                            y2_ind++;
                        }
                        y2_ind--;
                        z1_ind = zi.at(sys->conductorIn[ci - 1].zmin);
                        z2_ind = zi.at(sys->conductorIn[ci - 1].zmax);
                        for (myint indk = z1_ind; indk <= z2_ind; indk++) {
                            for (myint indj = x1_ind; indj <= x2_ind; indj++) {
                                for (myint indm = y1_ind; indm <= y2_ind; indm++) {
                                    if (sys->markNode[indk * sys->N_node_s + indj * (sys->N_cell_y + 1) + indm] == 0) {
                                        sys->markProSide.at(indk * sys->N_node_s + indj * (sys->N_cell_y + 1) + indm) = true;
                                    }
                                }
                            }
                        }
                    }

                }
                if (sys->conductorIn[ci - 1].x[indl] == sys->conductorIn[ci - 1].x[0]) {
                    //cout << "Made it to second if statement in auto ci" << endl;
                    x1 = sys->conductorIn[ci - 1].x[indl];
                    x2 = x1;
                    if (sys->conductorIn[ci - 1].y[indl] < sys->conductorIn[ci - 1].y[0]) {
                        y1 = sys->conductorIn[ci - 1].y[indl];
                        y2 = sys->conductorIn[ci - 1].y[0];
                    }
                    else {
                        y1 = sys->conductorIn[ci - 1].y[0];
                        y2 = sys->conductorIn[ci - 1].y[indl];
                    }
                    x1_ind = xi.at(x1);
                    x2_ind = xi.at(x2);
                    y1_ind = yi.at(y1);
                    y2_ind = yi.at(y2);
                    while (x1 - sys->xn[x1_ind] <= sideLen && x1_ind >= 0) {
                        x1_ind--;
                    }
                    x1_ind++;
                    while (sys->xn[x2_ind] - x2 <= sideLen && x2_ind < sys->nx) {
                        x2_ind++;
                    }
                    x2_ind--;
                    while (y1 - sys->yn[y1_ind] <= sideLen && y1_ind >= 0) {
                        y1_ind--;
                    }
                    y1_ind++;
                    while (sys->yn[y2_ind] - y2 <= sideLen && y2_ind < sys->ny) {
                        y2_ind++;
                    }
                    y2_ind--;
                    z1_ind = zi.at(sys->conductorIn[ci - 1].zmin);
                    z2_ind = zi.at(sys->conductorIn[ci - 1].zmax);
                    for (myint indk = z1_ind; indk <= z2_ind; indk++) {
                        for (myint indj = x1_ind; indj <= x2_ind; indj++) {
                            for (myint indm = y1_ind; indm <= y2_ind; indm++) {
                                if (sys->markNode[indk * sys->N_node_s + indj * (sys->N_cell_y + 1) + indm] == 0) {
                                    sys->markProSide.at(indk * sys->N_node_s + indj * (sys->N_cell_y + 1) + indm) = true;
                                }
                            }
                        }
                    }
                }
                else {
                    //cout << "Made it to second else statement in auto ci" << endl;
                    y1 = sys->conductorIn[ci - 1].y[indl];
                    y2 = y1;
                    if (sys->conductorIn[ci - 1].x[indl] < sys->conductorIn[ci - 1].x[0]) {
                        x1 = sys->conductorIn[ci - 1].x[indl];
                        x2 = sys->conductorIn[ci - 1].x[0];
                    }
                    else {
                        x1 = sys->conductorIn[ci - 1].x[0];
                        x2 = sys->conductorIn[ci - 1].x[indl];
                    }
                    x1_ind = xi.at(x1);
                    x2_ind = xi.at(x2);
                    y1_ind = yi.at(y1);
                    y2_ind = yi.at(y2);
                    while (x1 - sys->xn[x1_ind] <= sideLen && x1_ind >= 0) {
                        x1_ind--;
                    }
                    x1_ind++;
                    while (sys->xn[x2_ind] - x2 <= sideLen && x2_ind < sys->nx) {
                        x2_ind++;
                    }
                    x2_ind--;
                    while (y1 - sys->yn[y1_ind] <= sideLen && y1_ind >= 0) {
                        y1_ind--;
                    }
                    y1_ind++;
                    while (sys->yn[y2_ind] - y2 <= sideLen && y2_ind < sys->ny) {
                        y2_ind++;
                    }
                    y2_ind--;
                    z1_ind = zi.at(sys->conductorIn[ci - 1].zmin);
                    z2_ind = zi.at(sys->conductorIn[ci - 1].zmax);
                    //cout << "Did all index nonsense within second else statement in auto ci" << endl;
                    for (myint indk = z1_ind; indk <= z2_ind; indk++) {
                        for (myint indj = x1_ind; indj <= x2_ind; indj++) {
                            for (myint indm = y1_ind; indm <= y2_ind; indm++) {
                                /*if (indk * sys->N_node_s + indj * (sys->N_cell_y + 1) + indm >= sys->N_node) {
                                    cout << "Checking index " << indk * sys->N_node_s + indj * (sys->N_cell_y + 1) + indm << " of size-" << sys->N_node << " array markNode against 0" << endl;
                                    }*/
                                if (sys->markNode[indk * sys->N_node_s + indj * (sys->N_cell_y + 1) + indm] == 0) {
                                    sys->markProSide.at(indk * sys->N_node_s + indj * (sys->N_cell_y + 1) + indm) = true;
                                }
                            }
                        }
                    }
                    //cout << "Did all markProSide nonsense within second else statement in auto ci" << endl;
                }
            }
        }

#ifdef PRINT_VERBOSE_TIMING
        cout << "Time of finding side nodes is " << (clock() - t1) * 1.0 / CLOCKS_PER_SEC << " s" << endl;
#endif
        cout << "portSet dynamic execution time is " << (clock() - t0) * 1.0 / CLOCKS_PER_SEC << " s" << endl << endl;

#ifdef SKIP_WRITE_SYS_TO_FILE
        sys->conductorIn.clear(); // Using clear() does not reduce memory usage
#endif
    });
    taskPS3.name("Fine markProSide side nodes");
    taskPS2.precede(taskPS3);
    return 0;
}

/* Is point (x,y) within the polygon? */
bool polyIn(double x, double y, fdtdMesh *sys, int inPoly) {
    int npol;
    myint indi = 0, indj = 0;
    bool isCond = false;
    double disMin = 1.e-10;

    npol = sys->conductorIn[inPoly].numVert;

    for (myint indi = 0, indj = npol - 1; indi < npol; indj = indi++) {
        if ((abs(y - sys->conductorIn[inPoly].y[indj]) < disMin && abs(y - sys->conductorIn[inPoly].y[indi]) < disMin &&
            ((x >= sys->conductorIn[inPoly].x[indj] && x <= sys->conductorIn[inPoly].x[indi]) ||
            (x >= sys->conductorIn[inPoly].x[indi] && x <= sys->conductorIn[inPoly].x[indj])))) {
            return true;
        }
        else if (abs(x - sys->conductorIn[inPoly].x[indj]) < disMin && abs(x - sys->conductorIn[inPoly].x[indi]) < disMin &&
            ((y >= sys->conductorIn[inPoly].y[indj] && y <= sys->conductorIn[inPoly].y[indi]) ||
            (y >= sys->conductorIn[inPoly].y[indi] && y <= sys->conductorIn[inPoly].y[indj]))) {
            return true;
        }

        else if ((abs(sys->conductorIn[inPoly].y[indi] - sys->conductorIn[inPoly].y[indj]) > disMin &&
            (((sys->conductorIn[inPoly].y[indi] <= y) && (y < sys->conductorIn[inPoly].y[indj])) ||
            ((sys->conductorIn[inPoly].y[indj] <= y) && (y < sys->conductorIn[inPoly].y[indi])))) &&
                (x < (sys->conductorIn[inPoly].x[indj] - sys->conductorIn[inPoly].x[indi]) * (y - sys->conductorIn[inPoly].y[indi]) /
            (sys->conductorIn[inPoly].y[indj] - sys->conductorIn[inPoly].y[indi]) + sys->conductorIn[inPoly].x[indi])) {
            isCond = !isCond;
        }
    }
    return isCond;
}

// Print fdtdPort information
void fdtdPort::print() {
    // Print
    cout << " ------" << endl;
    cout << " Contents of fdtdPort" << endl;
    if (this->x == nullptr)
    {
        cout << "  x array exists (" << (this->x != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  x array has size " << NELEMENT(this->x) << endl;
    }
    if (this->y == nullptr)
    {
        cout << "  y array exists (" << (this->y != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  y array has size " << NELEMENT(this->y) << endl;
    }
    if (this->z == nullptr)
    {
        cout << "  z array exists (" << (this->z != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  z array has size " << NELEMENT(this->z) << endl;
    }
    for (size_t indSide = 0; indSide < this->multiplicity; indSide++)
    {
        cout << "   Coordinates of two points for the current source exciting side " << indSide + 1 << " out of " << this->multiplicity << ":" << endl;
        cout << "    Side " << indSide + 1 << " x-coordinates: " << this->x1[indSide] << " m and " << this->x2[indSide] << " m" << endl;
        cout << "    Side " << indSide + 1 << " y-coordinates: " << this->y1[indSide] << " m and " << this->y2[indSide] << " m" << endl;
        cout << "    Side " << indSide + 1 << " z-coordinates: " << this->z1[indSide] << " m and " << this->z2[indSide] << " m" << endl;
        if (this->portCnd.size() > 0)
        {
            cout << "   Side " << indSide + 1 << " has portCnd index #" << this->portCnd[indSide] << endl;
        }
        if (this->portEdge.size() > 0)
        {
            cout << "   Side " << indSide + 1 << " has " << this->portEdge[indSide].size() << " edges in the excitation" << endl;
        }
        if (this->portArea.size() > 0)
        {
            cout << "   Side " << indSide + 1 << " has an area of " << this->portArea[indSide] << " m^2" << endl;
        }
        cout << "    Side direction: " << this->portDirection[indSide] << endl;
    }
    if (this->node == nullptr)
    {
        cout << "  Node array exists (" << (this->node != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  Node array has size " << NELEMENT(this->node) << endl;
    }
    cout << " ------" << endl;
}

// Print fdtdMesh information
void fdtdMesh::print() {
    // Print
    cout << "------" << endl;
    cout << "Contents of fdtdMesh" << endl;
    cout << " Length unit: " << this->lengthUnit << " m" << endl;
    cout << " outedge: " << this->outedge << endl;
    cout << " inedge: " << this->inedge << endl;
    cout << " Frequency sweep parameters: " << endl;
    cout << "  Frequency unit: " << this->freqUnit << " Hz" << endl;
    cout << "  Starting frequency: " << this->freqStart << " * " << this->freqUnit << " Hz" << endl;
    cout << "  Ending frequency: " << this->freqEnd << " * " << this->freqUnit << " Hz" << endl;
    cout << "  Number of frequencies: " << this->nfreq << endl;
    cout << "  Frequency scaling (0 = logarithmic, 1 = linear): " << this->freqScale << endl;
    cout << " Node coordinate information:" << endl;
    cout << "  Number of nodes along direction: " << this->nx << " in x-dir, " << this->ny << " in y-dir, and " << this->nz << " in z-dir" << endl;
    cout << "  Node arrays: xn exists (" << (this->xn != nullptr) << "), yn exists (" << (this->yn != nullptr) << "), and zn exists (" << (this->zn != nullptr) << ")" << endl;
    cout << "  Node arrays: xnu exists (" << (this->xnu != nullptr) << "), ynu exists (" << (this->ynu != nullptr) << "), and znu exists (" << (this->znu != nullptr) << ")" << endl;
    cout << " Mesh cell information:" << endl;
    cout << "  Number in each direction: " << this->N_cell_x << " in x-dir, " << this->N_cell_y << " in y-dir, and " << this->N_cell_z << " in z-dir" << endl;
    cout << "  Limits in x-direction: " << this->xlim1 << " * " << this->lengthUnit << " m to " << this->xlim2 << " * " << this->lengthUnit << " m" << endl;
    cout << "  Limits in y-direction: " << this->ylim1 << " * " << this->lengthUnit << " m to " << this->ylim2 << " * " << this->lengthUnit << " m" << endl;
    cout << "  Limits in z-direction: " << this->zlim1 << " * " << this->lengthUnit << " m to " << this->zlim2 << " * " << this->lengthUnit << " m" << endl;
    cout << " Mesh edge information:" << endl;
    cout << "  Number of edges: " << this->N_edge << endl;
    cout << "  Number of edges_s: " << this->N_edge_s << endl;
    cout << "  Number of edges_v: " << this->N_edge_v << endl;
    cout << " Mesh node information:" << endl;
    cout << "  Number of nodes: " << this->N_node << endl;
    cout << "  Number of nodes_s: " << this->N_node_s << endl;
    cout << " Mesh patch information:" << endl;
    cout << "  Number of patches: " << this->N_patch << endl;
    cout << "  Number of patches_s: " << this->N_patch_s << endl;
    cout << "  Number of patches_v: " << this->N_patch_v << endl;
    cout << " Mesh field location information:" << endl;
    //cout << "  nodepos array exists (" << (this->nodepos != nullptr) << ")" << endl;
    cout << "  Epoints array exists (" << (this->Epoints != nullptr) << ")" << endl;
    //cout << "  edgelink array exists (" << (this->edgelink != nullptr) << ")" << endl;
    cout << "  Hpoints array exists (" << (this->Hpoints != nullptr) << ")" << endl;
    cout << "  nodeEdge vector has size " << this->nodeEdge.size() << endl;
    cout << "  nodeEdgea vector has size " << this->nodeEdgea.size() << endl;
    cout << " PEC information:" << endl;
    cout << "  Boundary node 1 array exists (" << (this->bd_node1 != nullptr) << ")" << endl;
    cout << "  Boundary node 2 array exists (" << (this->bd_node2 != nullptr) << ")" << endl;
    cout << "  Boundary edge array exists (" << (this->bd_edge != nullptr) << ")" << endl;
    cout << " Layer stack up parameters:" << endl;
    cout << "  Number of layers in stack: " << this->numStack << endl;
    cout << "  Relative permittivity vector has size " << this->stackEps.size() << endl;
    cout << "  Conductivity vector has size " << this->stackSig.size() << endl;
    cout << "  Beginning z-coordinate vector has size " << this->stackBegCoor.size() << endl;
    cout << "  Ending z-coordinate vector has size " << this->stackEndCoor.size() << endl;
    cout << "  Layer name vector has size " << this->stackName.size() << endl;
    cout << "  Edge permittivity diagonal matrix D_eps has size " << this->eps.size() << endl;
    cout << "  Ordered stackEpsn vector has size " << this->stackEpsn.size() << endl;
    cout << "  Ordered stackSign vector has size " << this->stackSign.size() << endl;
    if (this->stackCdtMark == nullptr)
    {
        cout << "  Stack conductor marker array exists (" << (this->stackCdtMark != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  Stack conductor marker array has size " << NELEMENT(this->stackCdtMark) << endl;
    }
    cout << " Conductor parameter information:" << endl;
    cout << "  conductorIn vector has size " << this->conductorIn.size() << endl;
    cout << "  Number of conductor rows: " << this->numCdtRow << endl;
    cout << "  Number of isolated conductors: " << this->numCdt << endl;
    if (this->markEdge == nullptr)
    {
        cout << "  Edge marker array exists (" << (this->markEdge != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  Edge marker array has size " << NELEMENT(this->markEdge) << endl;
    }
    if (this->markCell == nullptr)
    {
        cout << "  Cell marker array exists (" << (this->markCell != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  Cell marker array has size " << NELEMENT(this->markCell) << endl;
    }
    if (this->cdtNumNode == nullptr)
    {
        cout << "  cdtNumNode array exists (" << (this->cdtNumNode != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  cdtNumNode array has size " << NELEMENT(this->cdtNumNode) << endl;
    }
    if (this->sig == nullptr)
    {
        cout << "  Edge conductivity array D_sig exists (" << (this->sig != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  Edge conductivity array D_sig has size " << NELEMENT(this->sig) << endl;
    }
    if (this->conductor == nullptr)
    {
        cout << "  conductor array exists (" << (this->conductor != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  conductor array has size " << NELEMENT(this->conductor) << endl;
    }
    if (this->markNode == nullptr)
    {
        cout << "  Node marker array exists (" << (this->markNode != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  Node marker array has size " << NELEMENT(this->markNode) << endl;
    }
    cout << "  edgeCell vector has size " << this->edgeCell.size() << endl;
    cout << "  edgeCellArea vector has size " << this->edgeCellArea.size() << endl;
    cout << "  acu_cnno vector has size " << this->acu_cnno.size() << endl;
    cout << "  cindex vector has size " << this->cindex.size() << endl;
    cout << "  exciteCdtLayer array exists (" << (this->exciteCdtLayer != nullptr) << ")" << endl;
    cout << "  cond2condIn vector has size " << this->cond2condIn.size() << endl;
    cout << "  markProSide vector of side nodes by excited conductors has size " << this->markProSide.size() << endl;
    if (this->patch == nullptr)
    {
        cout << " Patch information exists (" << (this->patch != nullptr) << ")" << endl;
    }
    else
    {
        cout << " Patch information has size " << NELEMENT(this->patch) << endl;
    }
    if (this->bound == nullptr)
    {
        cout << " Boundary information exists (" << (this->bound != nullptr) << ")" << endl;
    }
    else
    {
        cout << " Boundary information has size " << NELEMENT(this->bound) << endl;
    }
    cout << " V0c information:" << endl;
    if (this->v0cval == nullptr)
    {
        cout << "  v0cRowId array exists (" << (this->v0cRowId != nullptr) << ")" << endl;
        cout << "  v0cColId array exists (" << (this->v0cColId != nullptr) << ")" << endl;
        cout << "  v0cColIdo array exists (" << (this->v0cColIdo != nullptr) << ")" << endl;
        cout << "  v0cval array exists (" << (this->v0cval != nullptr) << ")" << endl;
        cout << "  v0cvalo array exists (" << (this->v0cvalo != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  v0cRowId array has size " << NELEMENT(this->v0cRowId) << endl;
        cout << "  v0cColId array has size " << NELEMENT(this->v0cColId) << endl;
        cout << "  v0cColIdo array has size " << NELEMENT(this->v0cColIdo) << endl;
        cout << "  v0cval array has size " << NELEMENT(this->v0cval) << endl;
        cout << "  v0cvalo array has size " << NELEMENT(this->v0cvalo) << endl;
    }
    if (this->v0caval == nullptr)
    {
        cout << "  v0caRowId array exists (" << (this->v0caRowId != nullptr) << ")" << endl;
        cout << "  v0caColId array exists (" << (this->v0caColId != nullptr) << ")" << endl;
        cout << "  v0caColIdo array exists (" << (this->v0caColIdo != nullptr) << ")" << endl;
        cout << "  v0caval array exists (" << (this->v0caval != nullptr) << ")" << endl;
        cout << "  v0cavalo array exists (" << (this->v0cavalo != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  v0caRowId array has size " << NELEMENT(this->v0caRowId) << endl;
        cout << "  v0caColId array has size " << NELEMENT(this->v0caColId) << endl;
        cout << "  v0caColIdo array has size " << NELEMENT(this->v0caColIdo) << endl;
        cout << "  v0caval array has size " << NELEMENT(this->v0caval) << endl;
        cout << "  v0cavalo array has size " << NELEMENT(this->v0cavalo) << endl;
    }
    cout << " Arrays from technical paper information:" << endl;
    cout << "  v0c2y0c2 array exists (" << (this->v0c2y0c2 != nullptr) << ")" << endl;
    cout << "  v0c2y0c2o array exists (" << (this->v0c2y0c2o != nullptr) << ")" << endl;
    cout << "  yc array exists (" << (this->yc != nullptr) << ")" << endl;
    cout << "  v0cy0c array exists (" << (this->v0cy0c != nullptr) << ")" << endl;
    cout << " V0c' * D_sig * V0c information:" << endl;
    if (this->Acval == nullptr)
    {
        cout << "  AcRowId array exists (" << (this->AcRowId != nullptr) << ")" << endl;
        cout << "  AcRowId1 array exists (" << (this->AcRowId1 != nullptr) << ")" << endl;
        cout << "  AcColId array exists (" << (this->AcColId != nullptr) << ")" << endl;
        cout << "  Acval array exists (" << (this->Acval != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  AcRowId array has size " << NELEMENT(this->AcRowId) << endl;
        cout << "  AcRowId1 array has size " << NELEMENT(this->AcRowId1) << endl;
        cout << "  AcColId array has size " << NELEMENT(this->AcColId) << endl;
        cout << "  Acval array has size " << NELEMENT(this->Acval) << endl;
    }
    if (this->Adval == nullptr)
    {
        cout << "  AdRowId array exists (" << (this->AdRowId != nullptr) << ")" << endl;
        cout << "  AdRowId1 array exists (" << (this->AdRowId1 != nullptr) << ")" << endl;
        cout << "  AdColId array exists (" << (this->AdColId != nullptr) << ")" << endl;
        cout << "  Adval array exists (" << (this->Adval != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  AdRowId array has size " << NELEMENT(this->AdRowId) << endl;
        cout << "  AdRowId1 array has size " << NELEMENT(this->AdRowId1) << endl;
        cout << "  AdColId array has size " << NELEMENT(this->AdColId) << endl;
        cout << "  Adval array has size " << NELEMENT(this->Adval) << endl;
    }
    cout << "  crhs array exists (" << (this->crhs != nullptr) << ")" << endl;
    cout << " V0d_ information:" << endl;
    if (this->v0d1val == nullptr)
    {
        cout << "  v0d1RowId array exists (" << (this->v0d1RowId != nullptr) << ")" << endl;
        cout << "  v0d1ColId array exists (" << (this->v0d1ColId != nullptr) << ")" << endl;
        cout << "  v0d1ColIdo array exists (" << (this->v0d1ColIdo != nullptr) << ")" << endl;
        cout << "  v0d1val array exists (" << (this->v0d1val != nullptr) << ")" << endl;
        cout << "  v0d1valo array exists (" << (this->v0d1valo != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  v0d1RowId array has size " << NELEMENT(this->v0d1RowId) << endl;
        cout << "  v0d1ColId array has size " << NELEMENT(this->v0d1ColId) << endl;
        cout << "  v0d1ColIdo array has size " << NELEMENT(this->v0d1ColIdo) << endl;
        cout << "  v0d1val array has size " << NELEMENT(this->v0d1val) << endl;
        cout << "  v0d1valo array has size " << NELEMENT(this->v0d1valo) << endl;
    }
    if (this->v0d1aval == nullptr)
    {
        cout << "  v0d1aRowId array exists (" << (this->v0d1aRowId != nullptr) << ")" << endl;
        cout << "  v0d1aColId array exists (" << (this->v0d1aColId != nullptr) << ")" << endl;
        cout << "  v0d1aColIdo array exists (" << (this->v0d1aColIdo != nullptr) << ")" << endl;
        cout << "  v0d1aval array exists (" << (this->v0d1aval != nullptr) << ")" << endl;
        cout << "  v0d1avalo array exists (" << (this->v0d1avalo != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  v0d1aRowId array has size " << NELEMENT(this->v0d1aRowId) << endl;
        cout << "  v0d1aColId array has size " << NELEMENT(this->v0d1aColId) << endl;
        cout << "  v0d1aColIdo array has size " << NELEMENT(this->v0d1aColIdo) << endl;
        cout << "  v0d1aval array has size " << NELEMENT(this->v0d1aval) << endl;
        cout << "  v0d1avalo array has size " << NELEMENT(this->v0d1avalo) << endl;
    }
    if (this->v0d2val == nullptr)
    {
        cout << "  v0d2RowId array exists (" << (this->v0d2RowId != nullptr) << ")" << endl;
        cout << "  v0d2ColId array exists (" << (this->v0d2ColId != nullptr) << ")" << endl;
        cout << "  v0d2ColIdo array exists (" << (this->v0d2ColIdo != nullptr) << ")" << endl;
        cout << "  v0d2val array exists (" << (this->v0d2val != nullptr) << ")" << endl;
        cout << "  v0d2valo array exists (" << (this->v0d2valo != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  v0d2RowId array has size " << NELEMENT(this->v0d2RowId) << endl;
        cout << "  v0d2ColId array has size " << NELEMENT(this->v0d2ColId) << endl;
        cout << "  v0d2ColIdo array has size " << NELEMENT(this->v0d2ColIdo) << endl;
        cout << "  v0d2val array has size " << NELEMENT(this->v0d2val) << endl;
        cout << "  v0d2valo array has size " << NELEMENT(this->v0d2valo) << endl;
    }
    if (this->v0d2aval == nullptr)
    {
        cout << "  v0d2aRowId array exists (" << (this->v0d2aRowId != nullptr) << ")" << endl;
        cout << "  v0d2aColId array exists (" << (this->v0d2aColId != nullptr) << ")" << endl;
        cout << "  v0d2aColIdo array exists (" << (this->v0d2aColIdo != nullptr) << ")" << endl;
        cout << "  v0d2aval array exists (" << (this->v0d2aval != nullptr) << ")" << endl;
        cout << "  v0d2avalo array exists (" << (this->v0d2avalo != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  v0d2aRowId array has size " << NELEMENT(this->v0d2aRowId) << endl;
        cout << "  v0d2aColId array has size " << NELEMENT(this->v0d2aColId) << endl;
        cout << "  v0d2aColIdo array has size " << NELEMENT(this->v0d2aColIdo) << endl;
        cout << "  v0d2aval array has size " << NELEMENT(this->v0d2aval) << endl;
        cout << "  v0d2avalo array has size " << NELEMENT(this->v0d2avalo) << endl;
    }
    cout << "  yd array exists (" << (this->yd != nullptr) << ")" << endl;
    cout << " High-frequency components:" << endl;
    if (this->Vh == nullptr)
    {
        cout << "  Vh array exists (" << (this->Vh != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  Vh array has size " << NELEMENT(this->Vh) << endl;
    }
    cout << "  Number of Vh components: " << this->leng_Vh << endl;
    cout << " S matrix information:" << endl;
    cout << "  SRowId array exists (" << (this->SRowId != nullptr) << ")" << endl;
    cout << "  SColId array exists (" << (this->SColId != nullptr) << ")" << endl;
    cout << "  Sval array exists (" << (this->Sval != nullptr) << ")" << endl;
    cout << "  Value array for S matrix has length " << this->leng_S << endl;
    cout << " Solution storage information:" << endl;
    if (this->y == nullptr)
    {
        cout << "  y array exists (" << (this->y != nullptr) << ")" << endl;
    }
    else
    {
        cout << "  y array has size " << NELEMENT(this->y) << endl;
    }
    cout << "  x vector has size " << this->x.size() << endl;
    cout << " Port information for the " << this->numPorts << " ports:" << endl;
    for (size_t indPort = 0; indPort < this->numPorts; indPort++)
    {
        this->portCoor[indPort].print();
    }
    if (this->J == nullptr)
    {
        cout << " Current source array exists (" << (this->J != nullptr) << ")" << endl;
    }
    else
    {
        cout << " Current source array has size " << NELEMENT(this->J) << endl;
    }
    cout << " Current V0c,s^T*I information:" << endl;
    cout << "  v0csJ array exists (" << (this->v0csJ != nullptr) << ")" << endl;
    cout << "  Y array exists (" << (this->Y != nullptr) << ")" << endl;
    cout << "------" << endl;
}
