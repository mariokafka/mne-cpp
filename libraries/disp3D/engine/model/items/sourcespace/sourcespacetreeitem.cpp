//=============================================================================================================
/**
* @file     sourcespacetreeitem.cpp
* @author   Lorenz Esch <Lorenz.Esch@tu-ilmenau.de>;
*           Matti Hamalainen <msh@nmr.mgh.harvard.edu>
* @version  1.0
* @date     January, 2016
*
* @section  LICENSE
*
* Copyright (C) 2015, Lorenz Esch and Matti Hamalainen. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that
* the following conditions are met:
*     * Redistributions of source code must retain the above copyright notice, this list of conditions and the
*       following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
*       the following disclaimer in the documentation and/or other materials provided with the distribution.
*     * Neither the name of MNE-CPP authors nor the names of its contributors may be used
*       to endorse or promote products derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
* PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
* INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*
*
* @brief    SourceSpaceTreeItem class definition.
*
*/

//*************************************************************************************************************
//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "sourcespacetreeitem.h"
#include "../common/metatreeitem.h"
#include "../../3dhelpers/renderable3Dentity.h"
#include "../../3dhelpers/custommesh.h"
#include "../../3dhelpers/geometrymultiplier.h"
#include "../../materials/geometrymultipliermaterial.h"

#include <mne/mne_hemisphere.h>


//*************************************************************************************************************
//=============================================================================================================
// Qt INCLUDES
//=============================================================================================================

#include <Qt3DExtras/QSphereGeometry>
#include <QMatrix4x4>


//*************************************************************************************************************
//=============================================================================================================
// Eigen INCLUDES
//=============================================================================================================

#include <Eigen/Core>


//*************************************************************************************************************
//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace Eigen;
using namespace MNELIB;
using namespace DISP3DLIB;


//*************************************************************************************************************
//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

SourceSpaceTreeItem::SourceSpaceTreeItem(Qt3DCore::QEntity *p3DEntityParent, int iType, const QString& text)
: AbstractMeshTreeItem(p3DEntityParent, iType, text)
{
    initItem();
}


//*************************************************************************************************************

void SourceSpaceTreeItem::initItem()
{
    this->setEditable(false);
    this->setCheckable(true);
    this->setCheckState(Qt::Checked);
    this->setToolTip("Source space item");
}


//*************************************************************************************************************

void SourceSpaceTreeItem::addData(const MNEHemisphere& tHemisphere)
{
    //Create color from curvature information with default gyri and sulcus colors
    MatrixX3f matVertColor = createVertColor(tHemisphere.rr);

    //Set renderable 3D entity mesh and color data
    m_pCustomMesh->setMeshData(tHemisphere.rr,
                                tHemisphere.nn,
                                tHemisphere.tris,
                                matVertColor,
                                Qt3DRender::QGeometryRenderer::Triangles);

    //Add data which is held by this SourceSpaceTreeItem
    QVariant data;

    data.setValue(tHemisphere.rr);
    this->setData(data, Data3DTreeModelItemRoles::SurfaceVert);

    plotSources(tHemisphere);
}


//*************************************************************************************************************

void SourceSpaceTreeItem::plotSources(const MNEHemisphere& tHemisphere)
{
    Renderable3DEntity* pSourceSphereEntity = new Renderable3DEntity(this);

    //create geometry
    QSharedPointer<Qt3DExtras::QSphereGeometry> pSourceSphereGeometry = QSharedPointer<Qt3DExtras::QSphereGeometry>::create();
    pSourceSphereGeometry->setRadius(0.001f);
    //create instanced renderer
    GeometryMultiplier *pSphereMesh = new GeometryMultiplier(pSourceSphereGeometry);

    //Create transform matrix for each sphere instance
    QVector<QMatrix4x4> vTransforms;
    QVector3D tempPos;

    if(tHemisphere.isClustered())
    {
        vTransforms.reserve(tHemisphere.cluster_info.centroidVertno.size());

        for(int i = 0; i < tHemisphere.cluster_info.centroidVertno.size(); i++)
        {
            QMatrix4x4 tempTransform;
            const RowVector3f& sourcePos = tHemisphere.rr.row(tHemisphere.cluster_info.centroidVertno.at(i));

            tempPos.setX(sourcePos(0));
            tempPos.setY(sourcePos(1));
            tempPos.setZ(sourcePos(2));

            //Set position
            tempTransform.translate(tempPos);
            vTransforms.push_back(tempTransform);
        }
    }
    else
    {
        vTransforms.reserve(tHemisphere.vertno.rows());

        for(int i = 0; i < tHemisphere.vertno.rows(); i++)
        {
            QMatrix4x4 tempTransform;
            const RowVector3f& sourcePos = tHemisphere.rr.row(tHemisphere.vertno(i));

            tempPos.setX(sourcePos(0));
            tempPos.setY(sourcePos(1));
            tempPos.setZ(sourcePos(2));

            //Set position
            tempTransform.translate(tempPos);
            vTransforms.push_back(tempTransform);
        }
    }
    //Set instance Transform
    pSphereMesh->setTransforms(vTransforms);

    pSourceSphereEntity->addComponent(pSphereMesh);

    //Add material
    GeometryMultiplierMaterial* pMaterial = new GeometryMultiplierMaterial(true);
    QColor defaultColor(255,0,0);
    pMaterial->setAmbient(defaultColor);

    pSourceSphereEntity->addComponent(pMaterial);
}
