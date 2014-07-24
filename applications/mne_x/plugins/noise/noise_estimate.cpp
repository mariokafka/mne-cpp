//=============================================================================================================
/**
* @file     noise_estimate.cpp
* @author   Limin Sun <liminsun@nmr.mgh.harvard.edu>;
*           Christoph Dinh <chdinh@nmr.mgh.harvard.edu>;
*           Matti Hamalainen <msh@nmr.mgh.harvard.edu>
* @version  1.0
* @date     July, 2014
*
* @section  LICENSE
*
* Copyright (C) 2014, Limin Sun, Christoph Dinh and Matti Hamalainen. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that
* the following conditions are met:
*     * Redistributions of source code must retain the above copyright notice, this list of conditions and the
*       following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
*       the following disclaimer in the documentation and/or other materials provided with the distribution.
*     * Neither the name of the Massachusetts General Hospital nor the names of its contributors may be used
*       to endorse or promote products derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
* PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MASSACHUSETTS GENERAL HOSPITAL BE LIABLE FOR ANY DIRECT,
* INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*
*
* @brief    Contains the implementation of the NoiseEstimate class.
*
*/

//*************************************************************************************************************
//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "noise_estimate.h"
#include "FormFiles/noiseestimatesetupwidget.h"

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

using namespace NoiseEstimatePlugin;
using namespace MNEX;
using namespace XMEASLIB;


//*************************************************************************************************************
//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

NoiseEstimate::NoiseEstimate()
: m_bIsRunning(false)
, m_bProcessData(false)
, m_pRTMSAInput(NULL)
, m_pNEOutput(NULL)
, m_pBuffer(CircularMatrixBuffer<double>::SPtr())
{
}


//*************************************************************************************************************

NoiseEstimate::~NoiseEstimate()
{
    if(this->isRunning())
        stop();
}


//*************************************************************************************************************

QSharedPointer<IPlugin> NoiseEstimate::clone() const
{
    QSharedPointer<NoiseEstimate> pNoiseEstimateClone(new NoiseEstimate);
    return pNoiseEstimateClone;
}


//*************************************************************************************************************
//=============================================================================================================
// Creating required display instances and set configurations
//=============================================================================================================

void NoiseEstimate::init()
{
    // Input
    m_pRTMSAInput = PluginInputData<NewRealTimeMultiSampleArray>::create(this, "Noise Estimatge In", "Noise Estimate input data");
    connect(m_pRTMSAInput.data(), &PluginInputConnector::notify, this, &NoiseEstimate::update, Qt::DirectConnection);
    m_inputConnectors.append(m_pRTMSAInput);

    // Output
    m_pNEOutput = PluginOutputData<NoiseEstimation>::create(this, "Noise Estimate Out", "Noise Estimate output data");
    m_outputConnectors.append(m_pNEOutput);

//    m_pRTMSAOutput->data()->setMultiArraySize(100);
//    m_pRTMSAOutput->data()->setVisibility(true);

    //init channels when fiff info is available
    connect(this, &NoiseEstimate::fiffInfoAvailable, this, &NoiseEstimate::initConnector);

    //Delete Buffer - will be initailzed with first incoming data
    if(!m_pBuffer.isNull())
        m_pBuffer = CircularMatrixBuffer<double>::SPtr();
}


//*************************************************************************************************************

void NoiseEstimate::initConnector()
{
    qDebug() << "void NoiseEstimate::initConnector()";
//    if(m_pFiffInfo){
//        m_pNEOutput->data()->initFromFiffInfo(m_pFiffInfo);
//    }
//    else
//    {
//        m_iFFTlength = 0;
//        m_Fs = 0;
//    }


}


//*************************************************************************************************************

bool NoiseEstimate::start()
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

bool NoiseEstimate::stop()
{
    //Wait until this thread is stopped
    m_bIsRunning = false;

    if(m_bProcessData)
    {
        //In case the semaphore blocks the thread -> Release the QSemaphore and let it exit from the pop function (acquire statement)
        m_pBuffer->releaseFromPop();
        m_pBuffer->releaseFromPush();

        m_pBuffer->clear();

//        m_pNEOutput->data()->clear();
    }

    return true;
}


//*************************************************************************************************************

IPlugin::PluginType NoiseEstimate::getType() const
{
    return _IAlgorithm;
}


//*************************************************************************************************************

QString NoiseEstimate::getName() const
{
    return "NoiseEstimate Toolbox";
}


//*************************************************************************************************************

QWidget* NoiseEstimate::setupWidget()
{
    NoiseEstimateSetupWidget* setupWidget = new NoiseEstimateSetupWidget(this);//widget is later distroyed by CentralWidget - so it has to be created everytime new

    connect(this,&NoiseEstimate::SetNoisePara,setupWidget,&NoiseEstimateSetupWidget::init);
    //connect(this,&NoiseEstimate::RePlot,setupWidget,&NoiseEstimateSetupWidget::Update);

    return setupWidget;

}


//*************************************************************************************************************

void NoiseEstimate::update(XMEASLIB::NewMeasurement::SPtr pMeasurement)
{
    QSharedPointer<NewRealTimeMultiSampleArray> pRTMSA = pMeasurement.dynamicCast<NewRealTimeMultiSampleArray>();

    if(pRTMSA)
    {
        //Check if buffer initialized
        if(!m_pBuffer)
            m_pBuffer = CircularMatrixBuffer<double>::SPtr(new CircularMatrixBuffer<double>(64, pRTMSA->getNumChannels(), pRTMSA->getMultiArraySize()));

        //Fiff information
        if(!m_pFiffInfo)
        {
            m_pFiffInfo = pRTMSA->getFiffInfo();
            emit fiffInfoAvailable();
        }

        if(m_bProcessData)
        {
            MatrixXd t_mat(pRTMSA->getNumChannels(), pRTMSA->getMultiArraySize());

            for(qint32 i = 0; i < pRTMSA->getMultiArraySize(); ++i)
                t_mat.col(i) = pRTMSA->getMultiSampleArray()[i];

            m_pBuffer->push(&t_mat);
        }
    }
}



//*************************************************************************************************************

void NoiseEstimate::run()
{
    //
    // Read Fiff Info
    //
    while(!m_pFiffInfo)
        msleep(10);// Wait for fiff Info

    m_bProcessData = true;

    while (m_bIsRunning)
    {
        if(m_bProcessData)
        {

            if(m_pFiffInfo){
                //emit nFFT and sample rate to the setup widget
                m_iFFTlength = 4096;
                m_Fs = m_pFiffInfo.data()->sfreq;

                emit SetNoisePara(m_iFFTlength,m_Fs);
            }

            /* Dispatch the inputs */
            MatrixXd t_mat = m_pBuffer->pop();

            MatrixXd psdx(t_mat.rows(),m_iFFTlength/2+1);

            //ToDo: Implement your algorithm here
            for(qint32 i = 0; i < t_mat.rows(); ++i){//FFT calculation by row
                RowVectorXd data;//(t_mat.cols());
//                for(qint32 j=0; j<t_mat.cols();j++)
//                {
//                    data(j) = t_mat(i,j);
//                }

                data = t_mat.row(i);

                //zero-pad data to m_iFFTlength
                RowVectorXd t_dataZeroPad = RowVectorXd::Zero(m_iFFTlength);
                t_dataZeroPad.head(data.cols()) = data;

                //generate fft object
                Eigen::FFT<double> fft;
                fft.SetFlag(fft.HalfSpectrum);

                //fft-transform data sequence
                RowVectorXcd t_freqData;
                fft.fwd(t_freqData,t_dataZeroPad);

                // calculate spectrum from FFT
                RowVectorXcd xdft(m_iFFTlength/2+1);

                for(qint32 j=0; j<m_iFFTlength/2+1;j++)
                {                    
                    xdft(j) = t_freqData(j);
                    double mag_abs = t_freqData(i,j).real()* t_freqData(i,j).real() +  t_freqData(i,j).imag()*t_freqData(i,j).imag();

                    psdx(i,j) = (1.0/(m_Fs*m_iFFTlength))* mag_abs;
                }

                for(qint32 j=1; j<m_iFFTlength/2;j++)
                {
                    psdx(i,j) = 2.0*psdx(j);
                }


                //emit RePlot(psdx);

            }

//            std::cout << psdx(0,0) << std::endl;

//            for(qint32 i = 0; i < t_mat.cols(); ++i)
//                m_pRTMSAOutput->data()->setValue(t_mat.col(i));
        }
    }
}
