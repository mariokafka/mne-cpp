//=============================================================================================================
/**
* @file     rthpi.cpp
* @author   Chiran Doshi <chiran.doshi@childrens.harvard.edu>;
*           Matti Hamalainen <msh@nmr.mgh.harvard.edu>
* @version  1.0
* @date     June, 2014
*
* @section  LICENSE
*
* Copyright (C) 2014, Chiran Doshi and Matti Hamalainen. All rights reserved.
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
* @brief    Contains the implementation of the RtHpi class.
*
*/

//*************************************************************************************************************
//=============================================================================================================
// INCLUDES
//=============================================================================================================
#include <Eigen/Dense>
#include <iostream>
#include "rthpi.h"
#include "FormFiles/rthpisetupwidget.h"
#include <math.h>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>      // std::ifstream

//*************************************************************************************************************
//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QtCore/QtPlugin>
#include <QDebug>


//*************************************************************************************************************
//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace RtHpiPlugin;
using namespace MNEX;
using namespace XMEASLIB;


//*************************************************************************************************************
//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

RtHpi::RtHpi()
: m_bIsRunning(false)
, m_bProcessData(false)
, m_pRTMSAInput(NULL)
, m_pRTMSAOutput(NULL)
, m_pRtHpiBuffer(CircularMatrixBuffer<double>::SPtr())
{
}


//*************************************************************************************************************

RtHpi::~RtHpi()
{
    if(this->isRunning())
        stop();
}


//*************************************************************************************************************

QSharedPointer<IPlugin> RtHpi::clone() const
{
    QSharedPointer<RtHpi> pRtHpiClone(new RtHpi);
    return pRtHpiClone;
}


//*************************************************************************************************************
//=============================================================================================================
// Creating required display instances and set configurations
//=============================================================================================================

void RtHpi::init()
{
    // Input
    m_pRTMSAInput = PluginInputData<NewRealTimeMultiSampleArray>::create(this, "Rt HPI In", "RT HPI input data");
    connect(m_pRTMSAInput.data(), &PluginInputConnector::notify, this, &RtHpi::update, Qt::DirectConnection);
    m_inputConnectors.append(m_pRTMSAInput);

    // Output
    m_pRTMSAOutput = PluginOutputData<NewRealTimeMultiSampleArray>::create(this, "Rt HPI Out", "RT HPI output data");
    m_pRTMSAOutput->data()->setName(this->getName());//Provide name to auto store widget settings
    m_outputConnectors.append(m_pRTMSAOutput);

//    m_pRTMSAOutput->data()->setMultiArraySize(100);
//    m_pRTMSAOutput->data()->setVisibility(true);

    //init channels when fiff info is available
    connect(this, &RtHpi::fiffInfoAvailable, this, &RtHpi::initConnector);

    //Delete Buffer - will be initailzed with first incoming data
    if(!m_pRtHpiBuffer.isNull())
        m_pRtHpiBuffer = CircularMatrixBuffer<double>::SPtr();
}


//*************************************************************************************************************

void RtHpi::unload()
{

}


//*************************************************************************************************************

void RtHpi::initConnector()
{
    qDebug() << "void RtHpi::initConnector()";
    if(m_pFiffInfo)
        m_pRTMSAOutput->data()->initFromFiffInfo(m_pFiffInfo);
}


//*************************************************************************************************************

bool RtHpi::start()
{
    //Check if the thread is already or still running. This can happen if the start button is pressed immediately after the stop button was pressed. In this case the stopping process is not finished yet but the start process is initiated.
    if(this->isRunning())
        QThread::wait();

    m_bIsRunning = true;

    // Start threads
    QThread::start();

    return true;
}


//*************************************************************************************************************

bool RtHpi::stop()
{
    //Wait until this thread is stopped
    m_qMutex.lock();
    m_bIsRunning = false;
    m_qMutex.unlock();


    if(m_bProcessData)
    {
        //In case the semaphore blocks the thread -> Release the QSemaphore and let it exit from the pop function (acquire statement)
        m_pRtHpiBuffer->releaseFromPop();
        m_pRtHpiBuffer->releaseFromPush();

//        m_pRtHpiBuffer->clear();

//        m_pRTMSAOutput->data()->clear();
    }

    return true;
}


//*************************************************************************************************************

IPlugin::PluginType RtHpi::getType() const
{
    return _IAlgorithm;
}


//*************************************************************************************************************

QString RtHpi::getName() const
{
    return "RtHpi Toolbox";
}


//*************************************************************************************************************

QWidget* RtHpi::setupWidget()
{
    RtHpiSetupWidget* setupWidget = new RtHpiSetupWidget(this);//widget is later distroyed by CentralWidget - so it has to be created everytime new
    return setupWidget;
}


//*************************************************************************************************************

void RtHpi::update(XMEASLIB::NewMeasurement::SPtr pMeasurement)
{
    QSharedPointer<NewRealTimeMultiSampleArray> pRTMSA = pMeasurement.dynamicCast<NewRealTimeMultiSampleArray>();

    if(pRTMSA)
    {
        m_qMutex.lock();
        //Check if buffer initialized
        if(!m_pRtHpiBuffer)
            m_pRtHpiBuffer = CircularMatrixBuffer<double>::SPtr(new CircularMatrixBuffer<double>(8, pRTMSA->getNumChannels(), pRTMSA->getMultiSampleArray()[0].cols()));

        //Fiff information
        if(!m_pFiffInfo)
        {
            m_pFiffInfo = pRTMSA->info();
            emit fiffInfoAvailable();
        }

        m_qMutex.unlock();
        if(m_bProcessData)
        {
            MatrixXd t_mat;

            for(qint32 i = 0; i < pRTMSA->getMultiArraySize(); ++i)
            {
                t_mat = pRTMSA->getMultiSampleArray()[i];
                m_pRtHpiBuffer->push(&t_mat);
            }
        }
    }
}



//*************************************************************************************************************

void RtHpi::run()
{
    //
    // Read Fiff Info and wait if absent
    //
    bool waitForFiffInfo = true;
    while(waitForFiffInfo)
    {
        m_qMutex.lock();

        if(m_pFiffInfo) waitForFiffInfo = false;

        m_qMutex.unlock();
        msleep(10);// Wait for fiff Info
    }

    m_qMutex.lock();
    m_bProcessData = true;
    m_qMutex.unlock();


    struct sens sensors;
    struct coilParam coil;
    int numCoils = 4;
    int numCh = m_pFiffInfo->nchan;
    int samF = m_pFiffInfo->sfreq;
    int numLoc = 3, numBlock, samLoc, phase; // numLoc : Number of times to localize in a second
    samLoc = samF/numLoc; // minimum samples required to localize numLoc times in a second

    // Initialize HPI coils location and moment
    coil.pos = Eigen::MatrixXd::Zero(numCoils,3);
    coil.mom = Eigen::MatrixXd::Zero(numCoils,3);


    // Get the indices of reference channels
    QVector<int> refind(0);
    if (m_pFiffInfo->ch_names.indexOf("TRG013",0) >= 0) refind.append(m_pFiffInfo->ch_names.indexOf("TRG013",0)+1);
    if (m_pFiffInfo->ch_names.indexOf("TRG014",0) >= 0) refind.append(m_pFiffInfo->ch_names.indexOf("TRG014",0)+1);
    if (m_pFiffInfo->ch_names.indexOf("TRG015",0) >= 0) refind.append(m_pFiffInfo->ch_names.indexOf("TRG015",0)+1);
    if (m_pFiffInfo->ch_names.indexOf("TRG016",0) >= 0) refind.append(m_pFiffInfo->ch_names.indexOf("TRG016",0)+1);

    // Get the indices of inner layer channels
    QVector<int> innerind(0);
    int j = 0;
    for (int i = 0;i < numCh;i++) {
        if(m_pFiffInfo->chs[i].coil_type == 7002) {

            // Check if the sensor is bad, if not append to innerind
            if(!(m_pFiffInfo->bads.contains(m_pFiffInfo->ch_names.at(i)))) {
                innerind.append(j);
                j++;
            }

        }
    }

    // Initialize inner layer sensors
    sensors.coilpos = Eigen::MatrixXd::Zero(innerind.size(),3);
    sensors.coilori = Eigen::MatrixXd::Zero(innerind.size(),3);
    sensors.tra = Eigen::MatrixXd::Identity(innerind.size(),innerind.size());



    for(int i=0;i<innerind.size();i++) {
        sensors.coilpos(i,0) = m_pFiffInfo->chs[innerind.at(i)].loc(0,0);
        sensors.coilpos(i,1) = m_pFiffInfo->chs[innerind.at(i)].loc(1,0);
        sensors.coilpos(i,2) = m_pFiffInfo->chs[innerind.at(i)].loc(2,0);
        sensors.coilori(i,0) = m_pFiffInfo->chs[innerind.at(i)].loc(9,0);
        sensors.coilori(i,1) = m_pFiffInfo->chs[innerind.at(i)].loc(10,0);
        sensors.coilori(i,2) = m_pFiffInfo->chs[innerind.at(i)].loc(11,0);
    }

    //load polhemus HPI
    Eigen::MatrixXd headHPI(numCoils,3);
    for (int i=0;i<numCoils;i++) {
        headHPI(i,0) = m_pFiffInfo->dig.at(i+3).r[0];
        headHPI(i,1) = m_pFiffInfo->dig.at(i+3).r[1];
        headHPI(i,2) = m_pFiffInfo->dig.at(i+3).r[2];
    }

    std::cout<<headHPI(0,0)<<" "<<headHPI(0,1)<<" "<<headHPI(0,2)<<std::endl;


    QVector<MatrixXd> buffer;
    Eigen::MatrixXd amp(innerind.size(),numCoils);
    Eigen::Matrix4d trans;
    while (m_bIsRunning) {
        if(m_bProcessData) {

            /* Dispatch the inputs */

            MatrixXd t_mat = m_pRtHpiBuffer->pop();

            buffer.append(t_mat);

            if(buffer.size()*t_mat.cols() >= samLoc) {

                Eigen::MatrixXd alldata(t_mat.rows(),buffer.size()*t_mat.cols());
                // Concatenate data into a matrix
                for(int i=0;i<buffer.size();i++) alldata << buffer[i];

                // Get the data from inner layer channels
                Eigen::MatrixXd innerdata(innerind.size(),samLoc);
                Eigen::MatrixXd refdata(numCoils,samLoc);

                numBlock = alldata.cols()/samLoc;

                // Loop for localizing coils
                for(int i = 0;i<numBlock;i++) {

                    for(int j = 0;j < innerind.size();j++)
                        innerdata.row(j) << alldata.block(innerind[j],i*samLoc,1,samLoc);

                    for(int j = 0;j < refind.size();j++)
                        refdata.row(j) << alldata.block(refind[j],i*samLoc,1,samLoc);

                    // Once we have the required data, compute the amplitude and phase of the channels
                    for(int j = 0;j < numCoils;j++) {
                        for(int k = 0;k < innerind.size();k++) {
                            amp(k,j) = innerdata.row(k).cwiseProduct(refdata.row(j)).sum();
                            phase = amp(k,j)/abs(amp(k,j));
                            amp(k,j) = amp(k,j)*phase;
                            qDebug()<<"Amp "<<amp(k,j);
                        }
                    }



                    coil = dipfit(coil, sensors, amp, numCoils);

                    qDebug()<<"HPI head "<<headHPI(0,0)<<" "<<headHPI(0,1)<<" "<<headHPI(0,2);
                    qDebug()<<"HPI head "<<headHPI(1,0)<<" "<<headHPI(1,1)<<" "<<headHPI(1,2);
                    qDebug()<<"HPI head "<<headHPI(2,0)<<" "<<headHPI(2,1)<<" "<<headHPI(2,2);
                    qDebug()<<"HPI head "<<headHPI(3,0)<<" "<<headHPI(3,1)<<" "<<headHPI(3,2);


//                    qDebug()<<"HPI device "<<coil.pos(0,0)<<" "<<coil.pos(0,1)<<" "<<coil.pos(0,2);
//                    qDebug()<<"HPI device "<<coil.pos(1,0)<<" "<<coil.pos(1,1)<<" "<<coil.pos(1,2);
//                    qDebug()<<"HPI device "<<coil.pos(2,0)<<" "<<coil.pos(2,1)<<" "<<coil.pos(2,2);
//                    qDebug()<<"HPI device "<<coil.pos(3,0)<<" "<<coil.pos(3,1)<<" "<<coil.pos(3,2);

                    trans = computeTransformation(coil.pos,headHPI);
                    //qDebug()<<"Before: "<<m_pFiffInfo->dev_head_t.trans(0,0);

                    for(int ti =0; ti<4;ti++)
                        for(int tj=0;tj<4;tj++)
                    m_pFiffInfo->dev_head_t.trans(ti,tj) = trans(ti,tj);

                    //qDebug()<<"After: "<<m_pFiffInfo->dev_head_t.trans(0,0)<<trans(0,0);

                }

                buffer.clear();
            }

        }
    }
}

/*********************************************************************************
 * dipfit function is adapted from Fieldtrip Software. It has been
 * heavily edited for use with MNE-X Software
 *********************************************************************************/

coilParam RtHpi::dipfit(struct coilParam coil, struct sens sensors, Eigen::MatrixXd data, int numCoils)
{
    // Initialize variables
    int display = 0;
    int maxiter = 100;

    dipError temp;

    for(int i = 0;i<numCoils;i++) {
        coil.pos.row(i).array() = fminsearch(coil.pos.row(i), maxiter, 2 * maxiter * coil.pos.cols(), display, data.col(i), sensors);
        temp = dipfitError(coil.pos.row(i), data.col(i), sensors);
        coil.mom = temp.moment.transpose();
    }


    return coil;
}

/*********************************************************************************
 * fminsearch Multidimensional unconstrained nonlinear minimization (Nelder-Mead).
 * X = fminsearch(X0, maxiter, maxfun, display, data, sensors) starts at X0 and
 * attempts to find a local minimizer
 *********************************************************************************/

Eigen::MatrixXd RtHpi::fminsearch(Eigen::MatrixXd pos,int maxiter, int maxfun, int display, Eigen::MatrixXd data, struct sens sensors)
{
    double tolx, tolf, rho, chi, psi, sigma, func_evals, usual_delta, zero_term_delta, temp1, temp2;
    std::string header, how;
    int n, itercount, prnt;
    Eigen::MatrixXd onesn, two2np1, one2n, v, y, v1, tempX1, tempX2, xbar, xr, x, xe, xc, xcc, xin;
    std::vector <double> fv, fv1;
    std::vector <int> idx;

    dipError tempdip, fxr, fxe, fxc, fxcc;

    tolx = tolf = 1e-4;

    switch(display)
    {
        case 0:
            prnt = 0;
            break;
        default:
            prnt = 1;
    }

    header = " Iteration   Func-count     min f(x) Procedure";

    n = pos.cols();

    // Initialize parameters
    rho = 1; chi = 2; psi = 0.5; sigma = 0.5;
    onesn = Eigen::MatrixXd::Ones(1,n);
    two2np1 = one2n = Eigen::MatrixXd::Zero(1,n);

    for(int i = 0;i < n;i++) {
        two2np1(i) = 1 + i;
        one2n(i) = i;
    }

    v = v1 = Eigen::MatrixXd::Zero(n, n+1);
    fv.resize(n+1);idx.resize(n+1);fv1.resize(n+1);

    for(int i = 0;i < n; i++) v(i,0) = pos(i);

    tempdip = dipfitError(pos, data, sensors);
    fv[0] = tempdip.error;


    func_evals = 1;itercount = 0;how = "";

    // Continue setting up the initial simplex.
    // Following improvement suggested by L.Pfeffer at Stanford
    usual_delta = 0.05;             // 5 percent deltas for non-zero terms
    zero_term_delta = 0.00025;      // Even smaller delta for zero elements of x
    xin = pos.transpose();

    for(int j = 0;j < n;j++) {
        y = xin;

        if(y(j) != 0) y(j) = (1 + usual_delta) * y(j);
        else y(j) = zero_term_delta;

        v.col(j+1).array() = y;
        pos = y.transpose();
        tempdip = dipfitError(pos, data, sensors);
        fv[j+1] = tempdip.error;
    }

    // Sort elements of fv
    base_arr = fv;
    for (int i = 0; i < n+1; i++) idx[i] = i;

    sort (idx.begin(), idx.end(), RtHpi::compar);

    for (int i = 0;i < n+1;i++) {
        v1.col(i) = v.col(idx[i]);
        fv1[i] = fv[idx[i]];
    }

    v = v1;fv = fv1;

    how = "initial simplex";
    itercount = itercount + 1;
    func_evals = n + 1;

    tempX1 = Eigen::MatrixXd::Zero(1,n);

    while ((func_evals < maxfun) && (itercount < maxiter)) {
        for (int i = 0;i < n;i++) tempX1(i) = abs(fv[0] - fv[i+1]);
        temp1 = tempX1.maxCoeff();

        tempX2 = Eigen::MatrixXd::Zero(n,n);

        for(int i = 0;i < n;i++) tempX2.col(i) = v.col(i+1) -  v.col(0);

        tempX2 = tempX2.array().abs();

        temp2 = tempX2.maxCoeff();

        if((temp1 <= tolf) && (temp2 <= tolx)) break;

        xbar = v.block(0,0,n,n).rowwise().sum();
        xbar /= n;

        xr = (1+rho) * xbar - rho * v.block(0,n,v.rows(),1);

        x = xr.transpose();
        //std::cout << "Iteration Count: " << itercount << ":" << x << std::endl;

        fxr = dipfitError(x, data, sensors);

        func_evals = func_evals+1;

        if (fxr.error < fv[0]) {
            // Calculate the expansion point
            xe = (1 + rho * chi) * xbar - rho * chi * v.col(v.cols()-1);
            x = xe.transpose();
            fxe = dipfitError(x, data, sensors);
            func_evals = func_evals+1;

            if(fxe.error < fxr.error) {
                v.col(v.cols()-1) = xe;
                fv[n] = fxe.error;
                how = "expand";
            }
            else {
                v.col(v.cols()-1) = xr;
                fv[n] = fxr.error;
                how = "reflect";
            }
        }
        else {
            if(fxr.error < fv[n-1]) {
                v.col(v.cols()-1) = xr;
                fv[n] = fxr.error;
                how = "reflect";
            }
            else { // fxr.error >= fv[:,n-1]
                // Perform contraction
                if(fxr.error < fv[n]) {
                    // Perform an outside contraction
                    xc = (1 + psi * rho) * xbar - psi * rho * v.col(v.cols()-1);
                    x = xc.transpose();
                    fxc = dipfitError(x, data, sensors);
                    func_evals = func_evals + 1;

                    if(fxc.error <= fxr.error) {
                        v.col(v.cols()-1) = xc;
                        fv[n] = fxc.error;
                        how = "contract outside";
                    }
                    else {
                        // perform a shrink
                        how = "shrink";
                    }
                }
                else {
                    xcc = (1 - psi) * xbar + psi * v.col(v.cols()-1);
                    x = xcc.transpose();
                    fxcc = dipfitError(x, data, sensors);
                    func_evals = func_evals+1;
                    if(fxcc.error < fv[n]) {
                        v.col(v.cols()-1) = xcc;
                        fv[n] = fxcc.error;
                        how = "contract inside";
                    }
                    else {
                        // perform a shrink
                        how = "shrink";
                    }
                }
                if(how.compare("shrink") == 0) {
                    for(int j = 1;j < n+1;j++) {
                        v.col(j).array() = v.col(0).array() + sigma * (v.col(j).array() - v.col(0).array());
                        x = v.col(j).array().transpose();
                        tempdip = dipfitError(x,data, sensors);
                        fv[j] = tempdip.error;
                    }
                }
            }
        }
        // Sort elements of fv
        base_arr = fv;
        for (int i = 0; i < n+1; i++) idx[i] = i;
        sort (idx.begin (), idx.end (), RtHpi::compar);
        for (int i = 0;i < n+1;i++) {
            v1.col(i) = v.col(idx[i]);
            fv1[i] = fv[idx[i]];
        }
        v = v1;fv = fv1;
        itercount = itercount + 1;
    }

    x = v.col(0).transpose();
    std::cout << x << std::endl;

    return x;
}


/*********************************************************************************
 * dipfitError computes the error between measured and model data
 * and can be used for non-linear fitting of dipole position.
 * The function has been compared with matlab dipfit_error and it gives
 * same output
 *********************************************************************************/

dipError RtHpi::dipfitError(Eigen::MatrixXd pos, Eigen::MatrixXd data, struct sens sensors)
{
    // Variable Declaration
    struct dipError e;
    Eigen::MatrixXd lf, dif;

    // Compute lead field for a magnetic dipole in infinite vacuum
    lf = ft_compute_leadfield(pos, sensors);

    e.moment = pinv(lf) * data;

    dif = data - lf * e.moment;

    e.error = dif.array().square().sum()/data.array().square().sum();

    return e;
}

/*********************************************************************************
 * ft_compute_leadfield computes a forward solution for a dipole in a a volume
 * conductor model. The forward solution is expressed as the leadfield
 * matrix (Nchan*3), where each column corresponds with the potential or field
 * distributions on all sensors for one of the x,y,z-orientations of the dipole.
 * The function has been compared with matlab ft_compute_leadfield and it gives
 * same output
 *********************************************************************************/

Eigen::MatrixXd RtHpi::ft_compute_leadfield(Eigen::MatrixXd pos, struct sens sensors)
{

    Eigen::MatrixXd pnt, ori, lf;

    pnt = sensors.coilpos; // position of each coil
    ori = sensors.coilori; // orientation of each coil

    lf = magnetic_dipole(pos, pnt, ori);

    lf = sensors.tra * lf;

    return lf;
}

/*********************************************************************************
 * magnetic_dipole leadfield for a magnetic dipole in an infinite medium
 * The function has been compared with matlab magnetic_dipole and it gives same output
 *********************************************************************************/

Eigen::MatrixXd RtHpi::magnetic_dipole(Eigen::MatrixXd pos, Eigen::MatrixXd pnt, Eigen::MatrixXd ori) {

    double u0 = 1e-7;
    int nchan;
    Eigen::MatrixXd r, r2, r5, x, y, z, mx, my, mz, Tx, Ty, Tz, lf, temp;

    nchan = pnt.rows();

    // Shift the magnetometers so that the dipole is in the origin
    pnt.array().col(0) -= pos(0);pnt.array().col(1) -= pos(1);pnt.array().col(2) -= pos(2);

    r = pnt.array().square().rowwise().sum().sqrt();

   r2 = r5 = x = y = z = mx = my = mz = Tx = Ty = Tz = lf = Eigen::MatrixXd::Zero(nchan,3);

    for(int i = 0;i < nchan;i++) {
            r2.row(i).array().fill(pow(r(i),2));
            r5.row(i).array().fill(pow(r(i),5));
    }

    for(int i = 0;i < nchan;i++) {
        x.row(i).array().fill(pnt(i,0));
        y.row(i).array().fill(pnt(i,1));
        z.row(i).array().fill(pnt(i,2));
    }
    mx.col(0).array().fill(1);my.col(1).array().fill(1);mz.col(2).array().fill(1);

    Tx = 3 * x.cwiseProduct(pnt) - mx.cwiseProduct(r2);
    Ty = 3 * y.cwiseProduct(pnt) - my.cwiseProduct(r2);
    Tz = 3 * z.cwiseProduct(pnt) - mz.cwiseProduct(r2);

    for(int i = 0;i < nchan;i++) {
        lf(i,0) = Tx.row(i).dot(ori.row(i));
        lf(i,1) = Ty.row(i).dot(ori.row(i));
        lf(i,2) = Tz.row(i).dot(ori.row(i));
    }

    for(int i = 0;i < nchan;i++) {
        for(int j = 0;j < 3;j++) {
            lf(i,j) = u0 * lf(i,j)/(4 * M_PI * r5(i,j));
        }
    }
    return lf;
}

bool RtHpi::compar (int a, int b){
  return ((base_arr)[a] < (base_arr)[b]);
}

Eigen::Matrix4d RtHpi::computeTransformation(Eigen::MatrixXd NH, Eigen::MatrixXd BT)
{
    Eigen::MatrixXd xdiff, ydiff, zdiff, C, Q;
    Eigen::Matrix4d trans = Eigen::Matrix4d::Identity(4,4), Rot = Eigen::Matrix4d::Zero(4,4), Trans = Eigen::Matrix4d::Identity(4,4);
    double meanx,meany,meanz,normf;

    for(int i = 0;i < 15;i++) {
        zdiff = NH.col(2) - BT.col(2);
        ydiff = NH.col(1) - BT.col(1);
        xdiff = NH.col(0) - BT.col(0);

        meanx=xdiff.mean();
        meany=ydiff.mean();
        meanz=zdiff.mean();

        for (int j=0;j<NH.rows();j++) {
            BT(j,0) = BT(j,0) + meanx;
            BT(j,1) = BT(j,1) + meany;
            BT(j,2) = BT(j,2) + meanz;
        }

        C = BT.transpose() * NH;

        Eigen::JacobiSVD< Eigen::MatrixXd > svd(C ,Eigen::ComputeThinU | Eigen::ComputeThinV);

        Q = svd.matrixU() * svd.matrixV().transpose();

        BT = BT * Q;

        normf = (NH.transpose()-BT.transpose()).norm();

        for(int j=0;j<3;j++) {
            for(int k=0;k<3;k++) Rot(j,k) = Q(k,j);
        }

        Rot(3,3) = 1;
        Trans(0,3) = meanx;Trans(1,3) = meany;Trans(2,3) = meanz;
        trans = Rot * Trans * trans;
    }
    return trans;
}

Eigen::MatrixXd RtHpi::pinv(Eigen::MatrixXd a)
{
    double epsilon = std::numeric_limits<double>::epsilon();
    Eigen::JacobiSVD< Eigen::MatrixXd > svd(a ,Eigen::ComputeThinU | Eigen::ComputeThinV);
    double tolerance = epsilon * std::max(a.cols(), a.rows()) * svd.singularValues().array().abs()(0);
    return svd.matrixV() * (svd.singularValues().array().abs() > tolerance).select(svd.singularValues().array().inverse(),0).matrix().asDiagonal() * svd.matrixU().adjoint();
}
std::vector <double>RtHpi::base_arr;
