#include "filmImageProvider.h"
#include "../database/exifFunctions.h"
#include <pwd.h>
#include <unistd.h>
#include <QTimer>
#include <cmath>
#define TIMEOUT 0.1

FilmImageProvider::FilmImageProvider(ParameterManager * manager) :
    QObject(0),
    QQuickImageProvider(QQuickImageProvider::Image,
                        QQuickImageProvider::ForceAsynchronousImageLoading)
{
    paramManager = manager;
    zeroHistogram(finalHist);
    zeroHistogram(postFilmHist);
    zeroHistogram(preFilmHist);
    QObject::connect(paramManager, SIGNAL(paramChanged()), this, SLOT(abortPipeline()));
}

FilmImageProvider::~FilmImageProvider()
{
}

QImage FilmImageProvider::requestImage(const QString &id,
                                       QSize *size, const QSize &requestedSize)
{
    gettimeofday(&request_start_time,NULL);
    QImage output = emptyImage();

    //Copy out the latest parameters.
    paramMutex.lock();
    ProcessingParameters tempParams = paramManager->getParams();
    abort = false;
    paramMutex.unlock();

    writeDataMutex.lock();
    //Prepare the output filename.
    outputFilename = tempParams.filenameList[0].substr(0,tempParams.filenameList[0].length()-4);
    outputFilename.append("-output");


    //Run the pipeline.
    matrix<unsigned short> image = pipeline.processImage( tempParams, this, abort, exifData );
    writeDataMutex.unlock();

    int nrows = image.nr();
    int ncols = image.nc();

    output = QImage(ncols/3,nrows,QImage::Format_ARGB32);
    for(int i = 0; i < nrows; i++)
    {
        QRgb *line = (QRgb *)output.scanLine(i);
        for(int j = 0; j < ncols; j = j + 3)
        {
            *line = QColor(image(i,j)/256,
                           image(i,j+1)/256,
                           image(i,j+2)/256).rgb();
            line++;
        }
    }

    tout << "Request time: " << timeDiff(request_start_time) << " seconds" << endl;
    setProgress(1);
    *size = output.size();
    return output;
}

void FilmImageProvider::writeTiff()
{
    writeDataMutex.lock();
    imwrite_tiff(pipeline.getLastImage(), outputFilename, exifData);
    writeDataMutex.unlock();
}

void FilmImageProvider::writeJpeg()
{
    writeDataMutex.lock();
    matrix<unsigned short> outputData = pipeline.getLastImage();
    imwrite_jpeg(outputData, outputFilename, exifData, 95);
    writeDataMutex.unlock();
}

void FilmImageProvider::setProgress(float percentDone_in)
{
    progress = percentDone_in;
    emit progressChanged();
}

void FilmImageProvider::updateFilmProgress(float percentDone_in)//Percent filmulation
{
    progress = 0.2 + percentDone_in*0.6;
    emit progressChanged();
}

float FilmImageProvider::getHistogramPoint(Histogram &hist, int index, int i, LogY isLog)
{
    //index is 0 for L, 1 for R, 2 for G, and 3 for B.
    assert((index < 4) && (index >= 0));
    switch (index)
    {
    case 0: //luminance
        if (!isLog)
            return float(min(hist.lHist[i],hist.lHistMax))/float(hist.lHistMax);
        else
            return log(hist.lHist[i]+1)/log(hist.lHistMax+1);
    case 1: //red
        if (!isLog)
            return float(min(hist.rHist[i],hist.rHistMax))/float(hist.rHistMax);
        else
            return log(hist.rHist[i]+1)/log(hist.rHistMax+1);
    case 2: //green
        if (!isLog)
            return float(min(hist.gHist[i],hist.gHistMax))/float(hist.gHistMax);
        else
            return log(hist.gHist[i]+1)/log(hist.gHistMax+1);
    default://case 3: //blue
        if (!isLog)
            return float(min(hist.bHist[i],hist.bHistMax))/float(hist.bHistMax);
        else
            return log(hist.bHist[i]+1)/log(hist.bHistMax+1);
    }
    //xHistMax is the maximum height of any bin except the extremes.

    //return float(min(hist.allHist[i*4+index],hist.histMax[index]))/float(hist.histMax[index]); //maximum is the max of all elements except 0 and 127
}

QImage FilmImageProvider::emptyImage()
{
    return QImage(0,0,QImage::Format_ARGB32);
}
