/*
   Copyright 2012-2016 Ronald Römer

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <vtkKdTreePointLocator.h>
#include <vtkCubeSource.h>
#include <vtkCylinderSource.h>
#include <vtkSphereSource.h>
#include <vtkIdList.h>
#include <vtkIntArray.h>
#include <vtkCellData.h>
#include <vtkMath.h>
#include <vtkTrivialProducer.h>
#include <vtkTriangleFilter.h>
#include <vtkLinearSubdivisionFilter.h>
#include <vtkVectorText.h>
#include <vtkPolyDataNormals.h>
#include <vtkLinearExtrusionFilter.h>
#include <vtkPlaneSource.h>

#include <map>
#include <vector>
#include <set>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>

#include "vtkPolyDataBooleanFilter.h"

#include "Utilities.h"
using Utilities::IdsType;

//#define DD

typedef std::map<int, IdsType> LinksType;

class Test {
    vtkPolyData *lines, *pd;
    vtkKdTreePointLocator *loc;
    vtkIdList *cells, *poly, *pts;

    vtkIntArray *contsA, *contsB;

public:
    Test (vtkPolyData *pd, vtkPolyData *lines) : pd(pd), lines(lines) {
        loc = vtkKdTreePointLocator::New();
        loc->SetDataSet(pd);
        loc->BuildLocator();

        lines->BuildLinks();
        pd->BuildLinks();

        cells = vtkIdList::New();
        poly = vtkIdList::New();
        pts = vtkIdList::New();

        contsA = vtkIntArray::SafeDownCast(lines->GetCellData()->GetArray("cA"));
        contsB = vtkIntArray::SafeDownCast(lines->GetCellData()->GetArray("cB"));
    }
    ~Test () {
        loc->FreeSearchStructure();
        loc->Delete();
        cells->Delete();
        poly->Delete();
        pts->Delete();
    }

    int run () {

#ifdef DEBUG
        Utilities::WriteVTK("merged.vtk", pd);
#endif

        vtkIntArray *origCellIdsA = vtkIntArray::SafeDownCast(pd->GetCellData()->GetArray("OrigCellIdsA"));
        vtkIntArray *origCellIdsB = vtkIntArray::SafeDownCast(pd->GetCellData()->GetArray("OrigCellIdsB"));

        int errA = checkConnectivity(0, origCellIdsA);
        int errB = checkConnectivity(1, origCellIdsB);

        return errA > 0 || errB > 0;

    }

private:
    int checkConnectivity (int input, vtkIntArray *origIds) {
        std::cout << "Checking input " << input << std::endl;

        int err = 0;

        for (int i = 0; i < pd->GetNumberOfCells() && err < 1; i++) {
            if (origIds->GetValue(i) > -1) {
                pd->GetCellPoints(i, poly);
                if (poly->GetNumberOfIds() < 3) {
                    std::cout << "poly " << i << " has too few points" << std::endl;

                    err++;
                }
            }
        }

        if (err == 0) {

            int numLines = lines->GetNumberOfCells();

            double ptA[3], ptB[3];

            vtkIdList *line = vtkIdList::New();

            for (int i = 0; i < numLines; i++) {

                lines->GetCellPoints(i, line);

                lines->GetPoint(line->GetId(0), ptA);
                lines->GetPoint(line->GetId(1), ptB);

                Utilities::FindPoints(loc, ptA, pts);

                LinksType links;

                // sammelt polygone und deren punkte am jeweiligen ende der linie

                // bei einem normalen schnitt sollte jedes polygon zwei punkte haben

                for (int j = 0; j < pts->GetNumberOfIds(); j++) {
                    pd->GetPointCells(pts->GetId(j), cells);

                    for (int k = 0; k < cells->GetNumberOfIds(); k++) {
                        if (origIds->GetValue(cells->GetId(k)) > -1) {
                            links[cells->GetId(k)].push_back(pts->GetId(j));
                        }
                    }
                }

                Utilities::FindPoints(loc, ptB, pts);

                for (int j = 0; j < pts->GetNumberOfIds(); j++) {
                    pd->GetPointCells(pts->GetId(j), cells);

                    for (int k = 0; k < cells->GetNumberOfIds(); k++) {
                        if (origIds->GetValue(cells->GetId(k)) > -1) {
                            links[cells->GetId(k)].push_back(pts->GetId(j));
                        }
                    }
                }

                LinksType::const_iterator itr;

                IdsType polys;

                for (itr = links.begin(); itr != links.end(); ++itr) {
                    const IdsType &pts = itr->second;
                    if (pts.size() > 1) {
                        // gibt es doppelte punkte

                        pd->GetCellPoints(itr->first, poly);
                        int numPts = poly->GetNumberOfIds();

                        double a[3], b[3];
                        for (int j = 0; j < numPts; j++) {
                            pd->GetPoint(poly->GetId(j), a);
                            pd->GetPoint(poly->GetId((j+1)%numPts), b);

                            if (Utilities::GetD3(a, b) < 1e-6) {
                                std::cout << "poly " << itr->first << " has duplicated points" << std::endl;

                                err++;
                                break;
                            }
                        }

                        polys.push_back(itr->first);
                    }
                }

                if (err > 0) {
                    break;
                }

                if (polys.size() == 2) {
                    IdsType &f = links[polys[0]],
                            &s = links[polys[1]];

                    lines->GetPointCells(line->GetId(0), cells);
                    int linkedA = cells->GetNumberOfIds();

                    lines->GetPointCells(line->GetId(1), cells);
                    int linkedB = cells->GetNumberOfIds();

                    if (linkedA == linkedB) {
                        std::set<int> all;
                        all.insert(f.begin(), f.end());
                        all.insert(s.begin(), s.end());

                        if (all.size() < 4) {
                            err++;
                        }
                    } else if (linkedA == 4) {
                        if (f[0] == s[0]) {
                            err++;
                        }
                    } else {
                        if (f[1] == s[1]) {
                            err++;
                        }
                    }

                    if (err > 0) {
                        std::cout << "polys " << polys[0] << ", " << polys[1]
                            << " are connected" << std::endl;
                    }

                } else if (polys.size() == 1) {
                    IdsType &f = links[polys[0]];

                    if (f.size() < 3) {
                        std::cout << "line " << i << " has too few neighbors" << std::endl;
                        err++;
                    } else {
                        if (f[0] == f[1] || f[0] == f[2] || f[1] == f[2]) {
                            std::cout << "poly " << polys[0] << " is self connected" << std::endl;
                            err++;
                        }
                    }
                } else if (polys.size() == 0) {
                    std::cout << "line " << i << " has zero neighbors" << std::endl;
                    err++;
                }

                // sucht nach lücken

                IdsType::const_iterator itr2, itr3;

                for (int j = 0; j < 2 && err < 1; j++) {
                    double pt[3];
                    lines->GetPoint(line->GetId(j), pt);

                    IdsType ids;

                    Utilities::FindPoints(loc, pt, pts);
                    int numPts = pts->GetNumberOfIds();

                    for (int k = 0; k < numPts; k++) {
                        pd->GetPointCells(pts->GetId(k), cells);
                        int numCells = cells->GetNumberOfIds();

                        for (int l = 0; l < numCells; l++) {
                            if (origIds->GetValue(cells->GetId(l)) > -1) {

                                pd->GetCellPoints(cells->GetId(l), poly);
                                int numPts_ = poly->GetNumberOfIds();

                                for (int m = 0; m < numPts_; m++) {
                                    if (poly->GetId(m) == pts->GetId(k)) {
                                        ids.push_back(poly->GetId((m+1)%numPts_));
                                        ids.push_back(poly->GetId((m+numPts_-1)%numPts_));

                                        break;
                                    }
                                }
                            }
                        }

                    }

                    std::sort(ids.begin(), ids.end());

                    IdsType ids2 = ids;
                    ids2.erase(std::unique(ids2.begin(), ids2.end()), ids2.end());

                    IdsType ids3;
                    for (itr2 = ids2.begin(); itr2 != ids2.end(); ++itr2) {
                        if (std::count(ids.begin(), ids.end(), *itr2) == 1) {
                            ids3.push_back(*itr2);
                        }
                    }

                    int c = 0;

                    for (itr2 = ids3.begin(); itr2 != ids3.end(); ++itr2) {
                        for (itr3 = itr2+1; itr3 != ids3.end(); ++itr3) {
                            double a[3], b[3];
                            pd->GetPoint(*itr2, a);
                            pd->GetPoint(*itr3, b);

                            if (Utilities::GetD3(a, b) < 1e-6) {
                                c++;
                            }
                        }
                    }

                    if (ids3.size() != 2*c) {
                        std::cout << "line " << i << " has gaps at " << line->GetId(j) << std::endl;

                        err++;
                    }

                }

            }

            line->Delete();

        }

        return err;

    }

};

int main (int argc, char *argv[]) {
    std::istringstream stream(argv[1]);
    int t;

    stream >> t;

    if (t == 0) {
        vtkCubeSource *cu = vtkCubeSource::New();
        cu->SetYLength(.5);

        vtkCylinderSource *cyl = vtkCylinderSource::New();
        cyl->SetResolution(32);
        cyl->SetHeight(.5);
        cyl->SetCenter(0, .5, 0);

        vtkPolyDataBooleanFilter *bf = vtkPolyDataBooleanFilter::New();
        bf->SetInputConnection(0, cu->GetOutputPort());
        bf->SetInputConnection(1, cyl->GetOutputPort());

#ifndef DD
        bf->MergeAllOn();
#else
        bf->SetOperModeToDifference();
#endif

        bf->Update();

#ifndef DD
        Test test(bf->GetOutput(0), bf->GetOutput(1));
        int ok = test.run();
#else
        int ok = 0;
        Utilities::WriteVTK("test0.vtk", bf->GetOutput(0));
#endif

        bf->Delete();
        cyl->Delete();
        cu->Delete();

        return ok;

    } else if (t == 1) {
        vtkCubeSource *cu = vtkCubeSource::New();
        cu->SetYLength(.5);

        vtkCylinderSource *cyl = vtkCylinderSource::New();
        cyl->SetResolution(32);
        cyl->SetHeight(.5);
        cyl->SetCenter(0, .25, 0);

        vtkPolyDataBooleanFilter *bf = vtkPolyDataBooleanFilter::New();
        bf->SetInputConnection(0, cu->GetOutputPort());
        bf->SetInputConnection(1, cyl->GetOutputPort());

#ifndef DD
        bf->MergeAllOn();
#else
        bf->SetOperModeToDifference();
#endif

        bf->Update();

#ifndef DD
        Test test(bf->GetOutput(0), bf->GetOutput(1));
        int ok = test.run();
#else
        int ok = 0;
        Utilities::WriteVTK("test1.vtk", bf->GetOutput(0));
#endif

        bf->Delete();
        cyl->Delete();
        cu->Delete();

        return ok;

    } else if (t == 2) {
        vtkCubeSource *cu = vtkCubeSource::New();
        cu->SetYLength(.5);

        vtkCylinderSource *cyl = vtkCylinderSource::New();
        cyl->SetResolution(32);
        cyl->SetHeight(.5);

        vtkPolyDataBooleanFilter *bf = vtkPolyDataBooleanFilter::New();
        bf->SetInputConnection(0, cu->GetOutputPort());
        bf->SetInputConnection(1, cyl->GetOutputPort());

#ifndef DD
        bf->MergeAllOn();
#else
        bf->SetOperModeToDifference();
#endif

        bf->Update();

#ifndef DD
        Test test(bf->GetOutput(0), bf->GetOutput(1));
        int ok = test.run();
#else
        int ok = 0;
        Utilities::WriteVTK("test2.vtk", bf->GetOutput(0));
#endif

        bf->Delete();
        cyl->Delete();
        cu->Delete();

        return ok;

    } else if (t == 3) {
        vtkCubeSource *cubeA = vtkCubeSource::New();

        vtkCubeSource *cubeB = vtkCubeSource::New();
        cubeB->SetXLength(.5);
        cubeB->SetZLength(.5);
        cubeB->SetCenter(0, 0, .25);

        vtkPolyDataBooleanFilter *bf = vtkPolyDataBooleanFilter::New();
        bf->SetInputConnection(0, cubeA->GetOutputPort());
        bf->SetInputConnection(1, cubeB->GetOutputPort());

#ifndef DD
        bf->MergeAllOn();
#else
        bf->SetOperModeToDifference();
#endif

        bf->Update();

#ifndef DD
        Test test(bf->GetOutput(0), bf->GetOutput(1));
        int ok = test.run();
#else
        int ok = 0;
        Utilities::WriteVTK("test3.vtk", bf->GetOutput(0));
#endif

        bf->Delete();
        cubeB->Delete();
        cubeA->Delete();

        return ok;

    } else if (t == 4) {
        vtkCubeSource *cubeA = vtkCubeSource::New();

        vtkCubeSource *cubeB = vtkCubeSource::New();
        cubeB->SetCenter(0, 0, .75);

        vtkPolyDataBooleanFilter *bf = vtkPolyDataBooleanFilter::New();
        bf->SetInputConnection(0, cubeA->GetOutputPort());
        bf->SetInputConnection(1, cubeB->GetOutputPort());

#ifndef DD
        bf->MergeAllOn();
#else
        bf->SetOperModeToDifference();
#endif

        bf->Update();

#ifndef DD
        Test test(bf->GetOutput(0), bf->GetOutput(1));
        int ok = test.run();
#else
        int ok = 0;
        Utilities::WriteVTK("test4.vtk", bf->GetOutput(0));
#endif

        bf->Delete();
        cubeB->Delete();
        cubeA->Delete();

        return ok;

    } else if (t == 5) {
        // mehrer punkte in einem strip ohne fläche
         
        vtkCubeSource *cu = vtkCubeSource::New();

        vtkPolyData *cyl = vtkPolyData::New();
        cyl->Allocate(130, 1);

        vtkPoints *pts = vtkPoints::New();
        pts->SetNumberOfPoints(576);

        vtkIdList *top = vtkIdList::New();
        top->SetNumberOfIds(32);

        vtkIdList *bottom = vtkIdList::New();
        bottom->SetNumberOfIds(32);

        vtkIdList *poly = vtkIdList::New();
        poly->SetNumberOfIds(4);

        int ind = 0;

        for (int i = 0; i < 32; i++) {
            double x0 = .5*std::cos(i*2*pi/32);
            double z0 = .5*std::sin(i*2*pi/32);

            double x1 = .5*std::cos((i+1)*2*pi/32);
            double z1 = .5*std::sin((i+1)*2*pi/32);

            pts->SetPoint(ind, x0, .75, z0);
            top->SetId(i, ind++);

            pts->SetPoint(ind, x0, -.25, z0);
            bottom->SetId(i, ind++);

            for (int j = 0; j < 4; j++) {
                pts->SetPoint(ind, x0, -.25+j/4., z0);
                poly->SetId(0, ind++);
                pts->SetPoint(ind, x0, -.25+(j+1)/4., z0);
                poly->SetId(1, ind++);
                pts->SetPoint(ind, x1, -.25+(j+1)/4., z1);
                poly->SetId(2, ind++);
                pts->SetPoint(ind, x1, -.25+j/4., z1);
                poly->SetId(3, ind++);

                cyl->InsertNextCell(VTK_QUAD, poly);
            }
        }

        cyl->ReverseCell(cyl->InsertNextCell(VTK_POLYGON, top));
        cyl->InsertNextCell(VTK_POLYGON, bottom);

        poly->Delete();
        bottom->Delete();
        top->Delete();

        cyl->SetPoints(pts);

        vtkTrivialProducer *prod = vtkTrivialProducer::New();
        prod->SetOutput(cyl);

        vtkPolyDataBooleanFilter *bf = vtkPolyDataBooleanFilter::New();
        bf->SetInputConnection(0, cu->GetOutputPort());
        bf->SetInputConnection(1, prod->GetOutputPort());

#ifndef DD
        bf->MergeAllOn();
#else
        bf->SetOperModeToDifference();
#endif

        bf->Update();

#ifndef DD
        Test test(bf->GetOutput(0), bf->GetOutput(1));
        int ok = test.run();
#else
        int ok = 0;
        Utilities::WriteVTK("test5.vtk", bf->GetOutput(0));
#endif

        bf->Delete();
        prod->Delete();
        
        pts->Delete();
        cyl->Delete();
        cu->Delete();

        return ok;

    } else if (t == 6) {
        vtkCubeSource *cu = vtkCubeSource::New();

        vtkSphereSource *sp = vtkSphereSource::New();
        sp->SetRadius(.5);
        sp->SetCenter(.5, .5, .5);
        sp->SetPhiResolution(100);
        sp->SetThetaResolution(100);

        vtkPolyDataBooleanFilter *bf = vtkPolyDataBooleanFilter::New();
        bf->SetInputConnection(0, cu->GetOutputPort());
        bf->SetInputConnection(1, sp->GetOutputPort());

#ifndef DD
        bf->MergeAllOn();
#else
        bf->SetOperModeToDifference();
#endif

        bf->Update();

#ifndef DD
        Test test(bf->GetOutput(0), bf->GetOutput(1));
        int ok = test.run();
#else
        int ok = 0;
        Utilities::WriteVTK("test6.vtk", bf->GetOutput(0));
#endif

        bf->Delete();
        sp->Delete();
        cu->Delete();

        return ok;

    }  else if (t == 7) {
        // enthält schmale schnitte an den ecken der polygone

        vtkSphereSource *spA = vtkSphereSource::New();
        spA->SetRadius(50);
        spA->SetPhiResolution(6);
        spA->SetThetaResolution(6);
        

        vtkSphereSource *spB = vtkSphereSource::New();
        spB->SetRadius(50);
        spB->SetCenter(0, 25, 0);
        spB->SetPhiResolution(10);
        spB->SetThetaResolution(333);

        vtkPolyDataBooleanFilter *bf = vtkPolyDataBooleanFilter::New();
        bf->SetInputConnection(0, spA->GetOutputPort());
        bf->SetInputConnection(1, spB->GetOutputPort());

#ifndef DD
        bf->MergeAllOn();
#else
        bf->SetOperModeToDifference();
#endif

        bf->Update();

#ifndef DD
        Test test(bf->GetOutput(0), bf->GetOutput(1));
        int ok = test.run();
#else
        int ok = 0;
        Utilities::WriteVTK("test7.vtk", bf->GetOutput(0));
#endif

        bf->Delete();
        spB->Delete();
        spA->Delete();

        return ok;

    } else if (t == 8) {
        vtkSphereSource *spA = vtkSphereSource::New();
        spA->SetRadius(50);
        spA->SetPhiResolution(97);
        spA->SetThetaResolution(100);

        vtkSphereSource *spB = vtkSphereSource::New();
        spB->SetRadius(50);
        spB->SetCenter(25, 0, 0);
        spB->SetPhiResolution(81);
        spB->SetThetaResolution(195);

        vtkPolyDataBooleanFilter *bf = vtkPolyDataBooleanFilter::New();
        bf->SetInputConnection(0, spA->GetOutputPort());
        bf->SetInputConnection(1, spB->GetOutputPort());

#ifndef DD
        bf->MergeAllOn();
#else
        bf->SetOperModeToDifference();
#endif

        bf->Update();

#ifndef DD
        Test test(bf->GetOutput(0), bf->GetOutput(1));
        int ok = test.run();
#else
        int ok = 0;
        Utilities::WriteVTK("test8.vtk", bf->GetOutput(0));
#endif

        bf->Delete();
        spB->Delete();
        spA->Delete();

        return ok;

    } else if (t == 9) {
        // enthält sehr scharfwinklige schnitte mit kanten

        vtkSphereSource *spA = vtkSphereSource::New();
        spA->SetRadius(50);
        spA->SetPhiResolution(100);
        spA->SetThetaResolution(100);

        vtkSphereSource *spB = vtkSphereSource::New();
        spB->SetRadius(50);
        spB->SetCenter(25, 0, 0);
        spB->SetPhiResolution(251);
        spB->SetThetaResolution(251);

        vtkPolyDataBooleanFilter *bf = vtkPolyDataBooleanFilter::New();
        bf->SetInputConnection(0, spA->GetOutputPort());
        bf->SetInputConnection(1, spB->GetOutputPort());

#ifndef DD
        bf->MergeAllOn();
#else
        bf->SetOperModeToDifference();
#endif

        bf->Update();

#ifndef DD
        Test test(bf->GetOutput(0), bf->GetOutput(1));
        int ok = test.run();
#else
        int ok = 0;
        Utilities::WriteVTK("test9.vtk", bf->GetOutput(0));
#endif

        bf->Delete();
        spB->Delete();
        spA->Delete();

        return ok;

    } else if (t == 10) {
        // strip liegt auf einer kante und beginnt im gleichen punkt

        vtkCubeSource *cu = vtkCubeSource::New();

        vtkCylinderSource *cyl = vtkCylinderSource::New();
        cyl->SetResolution(32);
        cyl->SetRadius(.25);
        cyl->SetCenter(.25, 0, 0);

        vtkPolyDataBooleanFilter *bf = vtkPolyDataBooleanFilter::New();
        bf->SetInputConnection(0, cu->GetOutputPort());
        bf->SetInputConnection(1, cyl->GetOutputPort());

#ifndef DD
        bf->MergeAllOn();
#else
        bf->SetOperModeToDifference();
#endif

        bf->Update();

#ifndef DD
        Test test(bf->GetOutput(0), bf->GetOutput(1));
        int ok = test.run();
#else
        int ok = 0;
        Utilities::WriteVTK("test10.vtk", bf->GetOutput(0));
#endif

        bf->Delete();
        cyl->Delete();
        cu->Delete();

        return ok;

    } else if (t == 11) {
        vtkCubeSource *cubeA = vtkCubeSource::New();

        vtkTriangleFilter *tf = vtkTriangleFilter::New();
        tf->SetInputConnection(cubeA->GetOutputPort());

        vtkLinearSubdivisionFilter *sf = vtkLinearSubdivisionFilter::New();
        sf->SetInputConnection(tf->GetOutputPort());
        sf->SetNumberOfSubdivisions(4);

        vtkCubeSource *cubeB = vtkCubeSource::New();
        cubeB->SetXLength(.5);
        cubeB->SetZLength(.5);
        cubeB->SetCenter(0, 0, .25);

        vtkPolyDataBooleanFilter *bf = vtkPolyDataBooleanFilter::New();
        bf->SetInputConnection(0, sf->GetOutputPort());
        bf->SetInputConnection(1, cubeB->GetOutputPort());

#ifndef DD
        bf->MergeAllOn();
#else
        bf->SetOperModeToDifference();
#endif

        bf->Update();

#ifndef DD
        Test test(bf->GetOutput(0), bf->GetOutput(1));
        int ok = test.run();
#else
        int ok = 0;
        Utilities::WriteVTK("test11.vtk", bf->GetOutput(0));
#endif

        bf->Delete();
        sf->Delete();
        tf->Delete();
        cubeB->Delete();
        cubeA->Delete();

        return ok;

    } else if (t == 12) {
        vtkVectorText *text = vtkVectorText::New();
        text->SetText("zippy84");

        vtkLinearExtrusionFilter *ef = vtkLinearExtrusionFilter::New();
        ef->SetInputConnection(text->GetOutputPort());
        ef->SetExtrusionTypeToNormalExtrusion();
        ef->SetVector(0, 0, 1);

        // TODO: vtkPolyDataBooleanFilter muss prüfen, ob polygone konsistent orientiert sind
        // -> AddAdjacentPoints und CombineRegions funktionieren sonst nicht
        vtkPolyDataNormals *nf = vtkPolyDataNormals::New();
        nf->SetInputConnection(ef->GetOutputPort());
        nf->FlipNormalsOn();
        nf->ConsistencyOn();

        nf->Update();

        double ext[6];
        nf->GetOutput()->GetPoints()->GetBounds(ext);

        double dx = ext[0]+(ext[1]-ext[0])/2,
            dy = ext[2]+(ext[3]-ext[2])/2;

        vtkCubeSource *cube = vtkCubeSource::New();
        cube->SetXLength(10);
        cube->SetYLength(5);
        cube->SetCenter(dx, dy, .5);

        vtkTriangleFilter *tf = vtkTriangleFilter::New();
        tf->SetInputConnection(cube->GetOutputPort());

        vtkLinearSubdivisionFilter *sf = vtkLinearSubdivisionFilter::New();
        sf->SetInputConnection(tf->GetOutputPort());
        sf->SetNumberOfSubdivisions(3);

        vtkPolyDataBooleanFilter *bf = vtkPolyDataBooleanFilter::New();
        bf->SetInputConnection(0, nf->GetOutputPort());
        bf->SetInputConnection(1, sf->GetOutputPort());

#ifndef DD
        bf->MergeAllOn();
#else
        bf->SetOperModeToDifference2();
#endif

        bf->Update();

#ifndef DD
        Test test(bf->GetOutput(0), bf->GetOutput(1));
        int ok = test.run();
#else
        int ok = 0;
        Utilities::WriteVTK("test12.vtk", bf->GetOutput(0));
#endif

        bf->Delete();
        sf->Delete();
        tf->Delete();
        cube->Delete();
        ef->Delete();
        nf->Delete();
        text->Delete();

        return ok;

    } else if (t == 13) {
        vtkSphereSource *sp = vtkSphereSource::New();
        sp->SetRadius(.5);
        sp->SetPhiResolution(8);
        sp->SetThetaResolution(8);

        vtkPlaneSource *pl = vtkPlaneSource::New();
        pl->SetResolution(5, 5);

        vtkPolyDataBooleanFilter *bf = vtkPolyDataBooleanFilter::New();
        bf->SetInputConnection(0, sp->GetOutputPort());
        bf->SetInputConnection(1, pl->GetOutputPort());

#ifndef DD
        bf->MergeAllOn();
#else
        bf->SetOperModeToUnion();
#endif

        bf->Update();

#ifndef DD
        Test test(bf->GetOutput(0), bf->GetOutput(1));
        int ok = test.run();
#else
        int ok = 0;
        Utilities::WriteVTK("test13.vtk", bf->GetOutput(0));
#endif

        bf->Delete();
        pl->Delete();
        sp->Delete();

        return ok;
    }

}
