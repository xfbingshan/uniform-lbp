#include <set>
using namespace std;

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/ml.hpp>
using namespace cv;

#include "TextureFeature.h"


//
// find the number of unique labels, the class count
//
static int unique(const Mat &labels, set<int> &classes)
{
    for (size_t i=0; i<labels.total(); ++i)
        classes.insert(labels.at<int>(i));
    return classes.size();
}


static Mat tofloat(const Mat &src)
{
    if ( src.type() == CV_32F )
        return src;

    Mat query;
    src.convertTo(query,CV_32F);
    return query;
}


struct ClassifierNearest : public TextureFeature::Classifier
{
    Mat features;
    Mat labels;
    int flag;

    ClassifierNearest(int flag=NORM_L2) : flag(flag) {}

    virtual double distance(const cv::Mat &trainFeature, const cv::Mat &testFeature) const
    {
        return norm(trainFeature, testFeature, flag);
    }
    // TextureFeature::Classifier
    virtual int predict(const cv::Mat &testFeature, cv::Mat &results) const
    {
        double mind=DBL_MAX;
        int best = -1;
        for (int r=0; r<features.rows; r++)
        {
            double d = distance(testFeature, features.row(r));
            if (d < mind)
            {
                mind = d;
                best = r;
            }
        }
        int found = best>-1 ? labels.at<int>(best) : -1;
        results.push_back(float(found));
        results.push_back(float(mind));
        results.push_back(float(best));
        return 3;
    }

    virtual int train(const cv::Mat &trainFeatures, const cv::Mat &trainLabels)
    {
        features = trainFeatures;
        labels = trainLabels;
        return 1;
    }
};

struct ClassifierNearestFloat : public ClassifierNearest
{

    ClassifierNearestFloat(int flag=NORM_L2) : ClassifierNearest(flag) {}

    // TextureFeature::Classifier
    virtual int predict(const cv::Mat &testFeature, cv::Mat &results) const
    {
        return ClassifierNearest::predict(tofloat(testFeature), results);
    }
    virtual int train(const cv::Mat &trainFeatures, const cv::Mat &trainLabels)
    {
        return ClassifierNearest::train(tofloat(trainFeatures), trainLabels);
    }
};



//
// just swap the comparison
//   the flag enums are overlapping, so i like to have this in a different class
//
struct ClassifierHist : public ClassifierNearestFloat
{
    ClassifierHist(int flag=HISTCMP_CHISQR)
        : ClassifierNearestFloat(flag)
    {}

    // ClassifierNearest
    virtual double distance(const cv::Mat &trainFeature, const cv::Mat &testFeature) const
    {
         return compareHist(trainFeature, testFeature, flag);
    }
};


//
// Negated Mahalanobis Cosine Distance
//
struct ClassifierCosine : public ClassifierNearest
{
    virtual double distance(const cv::Mat &trainFeature, const cv::Mat &testFeature) const
    {
        double a = trainFeature.dot(testFeature);
        double b = trainFeature.dot(trainFeature);
        double c = testFeature.dot(testFeature);
        return -a / sqrt(b*c);
    }
};




struct ClassifierKNN : public TextureFeature::Classifier
{
    Ptr<ml::KNearest> knn;
    int K;

    ClassifierKNN(int k=1)
        : knn(ml::KNearest::create())
        , K(k)
    {}

    virtual int predict(const cv::Mat &testFeature, cv::Mat &results) const
    {
        Mat resp;
        knn->findNearest(testFeature, K, results, resp, Mat());
        //  std::cerr << "resp " << resp << std::endl;
        //for ( int k=0; k<resp.cols; k++ )
        //    results.push_back(resp.at<float>(k));
        return results.rows;
    }
    virtual int train(const cv::Mat &trainFeatures, const cv::Mat &trainLabels)
    {
        knn->clear();
        knn->train(trainFeatures, ml::ROW_SAMPLE, trainLabels);
        return 1;
    }
};



//
// single svm, multi class.
//
struct ClassifierSvm : public TextureFeature::Classifier
{
    Ptr<ml::SVM> svm;
    ml::SVM::Params param;


    ClassifierSvm(double degree = 0.5,double gamma = 0.8,double coef0 = 0,double C = 0.99, double nu = 0.002, double p = 0.5)
    //ClassifierSvm(double degree = 0.5,double gamma = 0.8,double coef0 = 0,double C = 0.99, double nu = 0.2, double p = 0.5)
    {
        param.kernelType = ml::SVM::POLY ; // CvSVM :: RBF , CvSVM :: LINEAR...
        param.svmType = ml::SVM::NU_SVC; // NU_SVC
        param.degree = degree; // for poly
        param.gamma = gamma; // for poly / rbf / sigmoid
        param.coef0 = coef0; // for poly / sigmoid
        param.C = C; // for CV_SVM_C_SVC , CV_SVM_EPS_SVR and CV_SVM_NU_SVR
        param.nu = nu; // for CV_SVM_NU_SVC , CV_SVM_ONE_CLASS , and CV_SVM_NU_SVR
        param.p = p; // for CV_SVM_EPS_SVR
        param.classWeights = NULL ; // for CV_SVM_C_SVC

        param.termCrit.type = TermCriteria::MAX_ITER | TermCriteria::EPS;
        param.termCrit.maxCount = 1000;
        param.termCrit.epsilon = 1e-6;
        svm = ml::SVM::create(param);
    }

    virtual int train(const Mat &src, const Mat &labels)
    {
        Mat trainData = tofloat(src.reshape(1,labels.rows));

        svm->clear();
        bool ok = svm->train(trainData , ml::ROW_SAMPLE , Mat(labels));
        // damn thing fails silently, if nu was not acceptable
        CV_Assert(ok&&"please check the input params(nu)");
        return trainData.rows;
    }

    virtual int predict(const Mat &src, Mat &res) const
    {
        svm->predict(tofloat(src), res);
        return res.rows;
    }
};



//
// single class(one vs. all), multi svm approach
//
struct ClassifierSvmMulti : public TextureFeature::Classifier
{
    vector< Ptr<ml::SVM> > svms;
    ml::SVM::Params param;

    ClassifierSvmMulti()
    {
        //
        // again, call me helpless on parameterizing this ;[
        //
        param.kernelType = ml::SVM::LINEAR; //, CvSVM::LINEAR...
        //param.svmType = ml::SVM::NU_SVC;
        ////param.degree = degree; // for poly
        param.gamma = 1.0; // for poly / rbf / sigmoid
        param.coef0 = 0.0; // for poly / sigmoid
        param.C = 0.5; // for CV_SVM_C_SVC , CV_SVM_EPS_SVR and CV_SVM_NU_SVR
        param.nu = 0.5; // for CV_SVM_NU_SVC , CV_SVM_ONE_CLASS , and CV_SVM_NU_SVR
        //param.p = p; // for CV_SVM_EPS_SVR
        //param.classWeights = NULL ; // for CV_SVM_C_SVC

        param.termCrit.type = TermCriteria::MAX_ITER | TermCriteria::EPS;
        param.termCrit.maxCount = 100;
        param.termCrit.epsilon = 1e-6;
    }

    virtual int train(const Mat &src, const Mat &labels)
    {
        svms.clear();

        Mat trainData = tofloat(src.reshape(1,labels.rows));
        //
        // train one svm per class:
        //
        set<int> classes;
        unique(labels,classes);

        for (set<int>::iterator it=classes.begin(); it != classes.end(); ++it)
        {
            Ptr<ml::SVM> svm = ml::SVM::create(param);
            Mat slabels; // you against all others, that's the only difference.
            for ( size_t j=0; j<labels.total(); ++j)
                slabels.push_back( (*it == labels.at<int>(j)) ? 1 : -1 );
            svm->train(trainData , ml::ROW_SAMPLE , slabels); // same data, different labels.
            svms.push_back(svm);
        }
        return trainData.rows;
    }


    virtual int predict(const Mat &src, Mat &res) const
    {
        Mat query = tofloat(src);
        //
        // predict per-class, return best(largest) result
        // hrmm, this assumes, the labels are [0..N]
        //
        float m = -1.0f;
        float mi = 0.0f;
        for (size_t j=0; j<svms.size(); ++j)
        {
            Mat r;
            svms[j]->predict(query, r);
            float p = r.at<float>(0);
            if (p > m)
            {
                m = p;
                mi = float(j);
            }
        }
        res = (Mat_<float>(1,2) << mi, m);
        return res.rows;
    }
};


//
// ref impl of eigen / fisher faces
//   this is basically bytefish's code,
//   (terribly) condensed to the bare minimum
//
struct ClassifierEigen : public TextureFeature::Classifier
{
    vector<Mat> _projections;
    Mat _labels;
    Mat _eigenvectors;
    Mat _mean;
    int _num_components;

    ClassifierEigen(int num_components=0)
        : _num_components(num_components)
    {}

    Mat project(const Mat &in) const
    {
        return LDA::subspaceProject(_eigenvectors, _mean, in);
    }

    void save_projections(const Mat &data)
    {
        _projections.clear();
        for(int i=0; i<data.rows; i++)
        {
            _projections.push_back(project(data.row(i)));
        }
    }

    virtual int train(const Mat &data, const Mat &labels)
    {
        if((_num_components <= 0) || (_num_components > data.rows))
            _num_components = data.rows;

        PCA pca(data, Mat(), cv::PCA::DATA_AS_ROW, _num_components);

        //if ( 1 ) // whitening
        //{
        //    Mat m = Mat::zeros(pca.eigenvectors.size(), pca.eigenvectors.type());
        //    Mat m2; sqrt(m.diag(pca.eigenvalues), m2);
        //    m2 = 1.0 / m2;
        //    cerr << m2(Rect(0,0,10,10)) << endl;   
        //    gemm( m2, pca.eigenvectors,1,Mat(),0,_eigenvectors);
        //    cerr << _eigenvectors(Rect(0,0,10,10)) << endl;   
        //}
        _labels = labels;
        _mean   = pca.mean.reshape(1,1);
        transpose(pca.eigenvectors, _eigenvectors);
        save_projections(data);
        return 1;
    }

    virtual int predict(const cv::Mat &testFeature, cv::Mat &results) const
    {
        Mat query = project(testFeature.reshape(1,1));
        double minDist = DBL_MAX;
        int minClass = -1;
        int minId = -1;
        for (size_t idx=0; idx<_projections.size(); idx++)
        {
            double dist = norm(_projections[idx], query, NORM_L2);
            if (dist < minDist)
            {
                minId    = idx;
                minDist  = dist;
                minClass = _labels.at<int>((int)idx);
            }
        }
        results.push_back(float(minClass));
        results.push_back(float(minDist));
        results.push_back(float(minId));
        return 3;
    }
};


struct ClassifierFisher : public ClassifierEigen
{
    ClassifierFisher(int num_components=0)
        : ClassifierEigen(num_components)
    {}

    // recycled in VerifierFisher..
    void train_base(const Mat &data, const Mat &labels)
    {
        set<int> classes;
        int C = unique(labels,classes);
        int N = data.rows;
        if((_num_components <= 0) || (_num_components > (C-1))) // btw, why C-1 ?
            _num_components = (C-1);

        // step one, do pca on the original(pixel) data:
        PCA pca(data, Mat(), cv::PCA::DATA_AS_ROW, (N-C));
        _mean = pca.mean.reshape(1,1);

        // step two, do lda on data projected to pca space:
        Mat proj = LDA::subspaceProject(pca.eigenvectors.t(), _mean, data);
        LDA lda(proj, labels, min(_num_components,pca.eigenvectors.rows));

        // step three, combine both:
        Mat leigen;
        lda.eigenvectors().convertTo(leigen, pca.eigenvectors.type());
        gemm(pca.eigenvectors, leigen, 1.0, Mat(), 0.0, _eigenvectors, GEMM_1_T);
    }

    virtual int train(const Mat &data, const Mat &labels)
    {
        train_base(data,labels);

        // step four, project training images to lda space for prediction:
        _labels = labels;
        save_projections(data);
        return 1;
    }
};




//
// train a single threshold value
//
struct VerifierNearest : TextureFeature::Verifier
{
    double thresh;
    int flag;

    VerifierNearest(int f=NORM_L2)
        : thresh(0)
        , flag(f)
    {}

    virtual double distance(const Mat &a, const Mat &b) const
    {
        return norm(a,b,flag);
    }

    virtual int train(const Mat &features, const Mat &labels)
    {
        thresh = 0;
        double dSame=0, dNotSame=0;
        int    nSame=0, nNotSame=0;
        for (size_t i=0; i<labels.total()-1; i+=2)
        {
            //for (size_t j=0; j<labels.total()-1; j+=2)
            {
                //if (i==j) continue;
                int j = i+1;
                double d = distance(features.row(i), features.row(j));
                if (labels.at<int>(i) == labels.at<int>(j))
                {
                    dSame += d;
                    nSame ++;
                }
                else
                {
                    dNotSame += d;
                    nNotSame ++;
                }
            }
            cerr << i << "/" << labels.total() << '\r';
        }
        dSame    = (dSame/nSame);
        dNotSame = (dNotSame/nNotSame);
        double dt = dNotSame - dSame;
        thresh = dSame + dt*0.25; //(dSame + dNotSame) / 2;
        //cerr << dSame << " " << dNotSame << " " << thresh << "\t" << nSame << " " << nNotSame <<  endl;
        return 1;
    }

    virtual bool same( const Mat &a, const Mat &b ) const
    {
        return (distance(a,b) < thresh);
    }
};


struct VerifierHist : VerifierNearest
{
    VerifierHist(int f=HISTCMP_CHISQR)
        : VerifierNearest(f)
    {}
    virtual double distance(const Mat &a, const Mat &b) const
    {
        return compareHist(a,b,flag);
    }
};


struct VerifierFisher
    : public virtual VerifierNearest
    , public virtual ClassifierFisher
{
    VerifierFisher(int flag, int num_components=0)
        : VerifierNearest(flag)
        , ClassifierFisher(num_components)
    {}

    virtual int train(const Mat &data, const Mat &labels)
    {
        ClassifierFisher::train_base(data,labels);

        Mat projections;
        for(int i=0; i<data.rows; i++)
            projections.push_back(project(data.row(i)));

        VerifierNearest::train(projections, labels);
        return 1;
    }

    virtual bool same(const Mat &a, const Mat &b) const
    {
        Mat pa = project(a);
        Mat pb = project(b);
        double d = distance(pa, pb);
        return d < thresh;
    }
};



//
// Wolf, Hassner, Taigman : "Descriptor Based Methods in the Wild"
//  4.1 Distance thresholding for pair matching
//
//  base class for svm,em,lr
//

template < class LabelType >
struct VerifierPairDistance : public TextureFeature::Verifier
{
    Ptr<ml::StatModel> model;
    int dist_flag;
    float scale;

    VerifierPairDistance(int df=2, float sca=0)
        : dist_flag(df)
        , scale(sca)
    {}

    Mat distance(const Mat &a, const Mat &b) const
    {
        Mat d;
        switch(dist_flag)
        {
            case 0: absdiff(a,b,d); break;
            case 1: d = a-b; multiply(d,d,d,1,CV_32F);
            case 2: d = a-b; multiply(d,d,d,1,CV_32F); cv::sqrt(d,d);
        }
        if (scale > 0)
            resize(d, d, Size(), scale, 1); // desperate to save memory
        return d;
    }

    virtual int train(const Mat &features, const Mat &labels)
    {
        Mat trainData = tofloat(features.reshape(1, labels.rows));

        Mat distances;
        Mat binlabels;
        for (size_t i=0; i<labels.total()-1; i+=2)
        {
            int j = i+1;
            distances.push_back(distance(trainData.row(i), trainData.row(j)));

            LabelType l = (labels.at<int>(i) == labels.at<int>(j)) ? LabelType(1) : LabelType(-1);
            binlabels.push_back(l);
        }
        model->clear();
        return model->train(ml::TrainData::create(distances, ml::ROW_SAMPLE, binlabels));
    }

    virtual bool same(const Mat &a, const Mat &b) const
    {
        Mat res;
        model->predict(distance(tofloat(a), tofloat(b)), res);
        LabelType r = res.at<LabelType>(0);
        return  r > 0;
    }
};

//
// binary (2 class) svm, same or not same based on distance
//
struct VerifierSVM : public VerifierPairDistance<int>
{
    VerifierSVM(int distFlag=2, float scale=0)
        : VerifierPairDistance<int>(distFlag, scale)
    {
        ml::SVM::Params param;
        param.kernelType = ml::SVM::LINEAR;
        param.svmType = ml::SVM::NU_SVC;
        param.C = 1;
        param.nu = 0.5;

        param.termCrit.type = TermCriteria::MAX_ITER | TermCriteria::EPS;
        param.termCrit.maxCount = 1000;
        param.termCrit.epsilon = 1e-6;

        model = ml::SVM::create(param);
    }
};


//
// the only restricted / unsupervised case!
//
struct VerifierEM : public VerifierPairDistance<int>
{
    VerifierEM(int distFlag=2, float scale=0)
        : VerifierPairDistance<int>(distFlag,scale)
    {}

    virtual int train(const Mat &features, const Mat &labels)
    {
        Mat trainData = tofloat(features.reshape(1, labels.rows));
        Mat distances;
        for (size_t i=0; i<labels.total()-1; i+=2)
        {
            int j = i+1;
            distances.push_back(distance(trainData.row(i), trainData.row(j)));
        }

        ml::EM::Params param;
        param.nclusters = 2;
        param.covMatType = ml::EM::COV_MAT_DIAGONAL;
        param.termCrit.type = TermCriteria::MAX_ITER | TermCriteria::EPS;
        param.termCrit.maxCount = 1000;
        param.termCrit.epsilon = 1e-6;

        model = ml::EM::train(distances,noArray(),noArray(),noArray(),param);
        return 1;
    }

    virtual bool same(const Mat &a, const Mat &b) const
    {
        Mat fa = tofloat(a);
        Mat fb = tofloat(b);
        float s = model->predict(distance(fa, fb));
        //cerr << s << " ";
        return s>=1;
    }
};


//
// crashes weirdly , atm.
//
struct VerifierBoost : public VerifierPairDistance<int>
{
    VerifierBoost(int distFlag=2, float scale=0)
        : VerifierPairDistance<int>(distFlag, scale)
    {
        ml::Boost::Params param;
        param.boostType = ml::Boost::DISCRETE;
        param.weightTrimRate = 0.6;
        model = ml::Boost::create(param);
    }
};



struct VerifierLR : public VerifierPairDistance<float>
{
    VerifierLR(int distFlag=2, float scale=0)
        : VerifierPairDistance<float>(distFlag,scale)
    {
        ml::LogisticRegression::Params params;
        params.alpha = 0.005;
        params.num_iters = 10000;
        params.norm = ml::LogisticRegression::REG_L2;
        params.regularized = 1;
        //params.train_method = ml::LogisticRegression::MINI_BATCH;
        //params.mini_batch_size = 10;
        model = ml::LogisticRegression::create(params);
    }
};



//
// 'factory' functions (aka public api)
//
// (identification)
//
//

cv::Ptr<TextureFeature::Classifier> createClassifierNearest(int norm_flag)
{ return makePtr<ClassifierNearest>(norm_flag); }

cv::Ptr<TextureFeature::Classifier> createClassifierHist(int flag)
{ return makePtr<ClassifierHist>(flag); }

cv::Ptr<TextureFeature::Classifier> createClassifierCosine()
{ return makePtr<ClassifierCosine>(); }

cv::Ptr<TextureFeature::Classifier> createClassifierKNN(int k)
{ return makePtr<ClassifierKNN>(k); }

cv::Ptr<TextureFeature::Classifier> createClassifierSVM(double degree, double gamma, double coef0, double C, double nu, double p)
{ return makePtr<ClassifierSvm>(degree, gamma, coef0, C, nu, p); }

cv::Ptr<TextureFeature::Classifier> createClassifierSVMMulti()
{ return makePtr<ClassifierSvmMulti>(); }

//
// reference impl
//
cv::Ptr<TextureFeature::Classifier> createClassifierEigen()
{ return makePtr<ClassifierEigen>(); }

cv::Ptr<TextureFeature::Classifier> createClassifierFisher()
{ return makePtr<ClassifierFisher>(); }


//
// (verification)
//

cv::Ptr<TextureFeature::Verifier> createVerifierNearest(int norm_flag)
{ return makePtr<VerifierNearest>(norm_flag); }

cv::Ptr<TextureFeature::Verifier> createVerifierHist(int norm_flag)
{ return makePtr<VerifierHist>(norm_flag); }

cv::Ptr<TextureFeature::Verifier> createVerifierFisher(int norm_flag)
{ return makePtr<VerifierFisher>(norm_flag); }

cv::Ptr<TextureFeature::Verifier> createVerifierSVM(int distfunc, float scale)
{ return makePtr<VerifierSVM>(distfunc,scale); }

cv::Ptr<TextureFeature::Verifier> createVerifierEM(int distfunc, float scale)
{ return makePtr<VerifierEM>(distfunc,scale); }

cv::Ptr<TextureFeature::Verifier> createVerifierLR(int distfunc, float scale)
{ return makePtr<VerifierLR>(distfunc,scale); }

cv::Ptr<TextureFeature::Verifier> createVerifierBoost(int distfunc, float scale)
{ return makePtr<VerifierBoost>(distfunc,scale); }

