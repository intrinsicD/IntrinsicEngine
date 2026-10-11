// Offline METHOD-079 snapshot replay; long double tests sensitivity, not exact arithmetic.
#include <Eigen/Dense>
#include <algorithm>
#include <cassert>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
using R = long double;
using V = Eigen::Matrix<R, Eigen::Dynamic, 1>;
using M = Eigen::Matrix<R, Eigen::Dynamic, Eigen::Dynamic>;
using E = Eigen::SelfAdjointEigenSolver<M>;
constexpr R cutoff = 1e-10L;
R kernel(const M& a, Eigen::Index i, const M& b, Eigen::Index j, R sigma2) {
    return std::exp(-(a.row(i)-b.row(j)).squaredNorm()/(2*sigma2));
}
M gram(const M& z, R sigma2) {
    M w(z.rows(),z.rows());
    for (Eigen::Index i=0;i<z.rows();++i) for(Eigen::Index j=0;j<z.rows();++j) w(i,j)=kernel(z,i,z,j,sigma2);
    return w;
}
V row(const M& x, const M& z, R sigma2) {
    V k(z.rows());for(Eigen::Index j=0;j<z.rows();++j) k[j]=kernel(x,0,z,j,sigma2);return k;
}
V inverse_values(const V& values) {
    const R top=values.maxCoeff();
    V d(values.size());
    for(Eigen::Index i=0;i<values.size();++i) {
        d[i]=values[i]>cutoff*top ? 1/values[i] : 0;
    }
    return d;
}
V apply(const E& e, const V& a) {
    if(e.info()!=Eigen::Success) throw std::runtime_error("eigensolver failed");
    return e.eigenvectors()*(inverse_values(e.eigenvalues()).array()*(e.eigenvectors().transpose()*a).array()).matrix();
}
std::string classification(R v) {

    return !std::isfinite(v)?"nonfinite":v<0?"negative":v==0?"zero":"positive";
}
void emit(const char* key,R v) { std::cout<<key<<'\t'<<std::setprecision(21)<<v<<'\n'; }
void spectrum(const char* prefix,const V& d) {
    R threshold=cutoff*d.maxCoeff(), low=std::numeric_limits<R>::infinity(),near=low;
    std::size_t rank=0,negative=0;
    for(R x:d) { if(x>threshold) {++rank;low=std::min(low,x);} negative+=x<0;near=std::min(near,std::abs(x/threshold-1)); }
    const auto out=[&](const char* key,R v){emit((std::string(prefix)+key).c_str(),v);};
    out("_rank",rank);out("_negative_eigenvalues",negative);out("_top",d.maxCoeff());out("_smallest",d.minCoeff());
    out("_smallest_retained",low);out("_retained_condition",d.maxCoeff()/low);out("_cutoff_relative_margin",near);
}
void self_test() {
    assert(std::numeric_limits<R>::digits>std::numeric_limits<double>::digits);
    assert(classification(-1)=="negative");assert(classification(0)=="zero");assert(classification(1)=="positive");
    const auto nativeExp=[](R v){return v>=-708 ? std::exp(v) : R(0);};
    assert(nativeExp(-709)==0 && std::exp(-709.0L)>0 && nativeExp(-708)>0);
    assert(classification(std::numeric_limits<R>::infinity())=="nonfinite");
    M z(2,3),x(1,3),y(1,3);z<<0,0,0,1,0,0;x<<-1,0,0;y<<2,0,0;
    E e(gram(z,.25L));R v=row(x,z,.25L).dot(apply(e,row(y,z,.25L)));
    assert(v<-.0024L && v>-.0025L);assert(e.eigenvalues().maxCoeff()/e.eigenvalues().minCoeff()<1.32L);
    assert(kernel(x,0,y,0,.25L)>0);
    x.row(0)=z.row(0); y=x;
    assert(std::abs(row(x,z,.25L).dot(apply(e,row(y,z,.25L)))-1)<1e-14L);
    M lone(1,3);lone<<0,0,0;x<<10,0,0;y=x;E one(gram(lone,1));
    assert(kernel(x,0,y,0,1)==1);assert(row(x,lone,1).dot(apply(one,row(y,lone,1)))<1e-40L);
    z<<0,0,0,1e-5L,0,0;E close(gram(z,1));assert((inverse_values(close.eigenvalues()).array()>0).count()==1);
    std::cout<<"PASS: positive, negative/well-conditioned, missing coverage and truncated near-dependence; classifier boundaries\n";
}
int main(int argc,char** argv) try {
    if(argc==2 && std::string(argv[1])=="--self-test") {self_test();return 0;}
    if(argc!=3) throw std::runtime_error("usage: analyze snapshot output-prefix");
    std::ifstream in(argv[1],std::ios::binary);std::uint64_t h[5];
    if(!in.read(reinterpret_cast<char*>(h),sizeof(h)) || h[0]!=79 || h[1]<1 || h[1]>4096 || h[2]<1 || h[2]>2000000 || h[3]>=h[2]) throw std::runtime_error("bad snapshot header");
    const Eigen::Index l=h[1],m=h[2];
    auto read=[&](Eigen::Index n){std::vector<double> a(n);if(!in.read(reinterpret_cast<char*>(a.data()),n*sizeof(double))) throw std::runtime_error("short snapshot");return Eigen::Map<Eigen::VectorXd>(a.data(),n).cast<R>().eval();};
    V s=read(7);M z(l,3),y(m,3),x(1,3);
    for(int c=0;c<3;++c)z.col(c)=read(l);for(int c=0;c<3;++c)y.col(c)=read(m);V lw=read(m);
    V raw=read(l*l);M w=Eigen::Map<M>(raw.data(),l,l);V d=read(l);raw=read(l*l);M u=Eigen::Map<M>(raw.data(),l,l);
    V mask=read(l),kx=read(l),a=read(l),g=read(l);raw=read(l*l);M pinv=Eigen::Map<M>(raw.data(),l,l);if(in.peek()!=EOF)throw std::runtime_error("extra snapshot bytes");
    if(!z.allFinite()||!y.allFinite()||!w.allFinite()||!u.allFinite()||!d.allFinite()||!lw.allFinite()||!(s[0]>0))throw std::runtime_error("nonfinite input operands");
    x<<s[3],s[4],s[5];const R sigma2=s[0];
    emit("long_double_digits",std::numeric_limits<R>::digits);emit("landmarks",l);emit("sources",m);emit("target_index",h[3]);emit("draw",h[4]);
    emit("sigma2_internal",sigma2);emit("logC",s[1]);emit("log_weight_shift",s[2]);emit("native_sum",s[6]);
    std::cout<<"native_class\t"<<classification(s[6])<<'\n';
    double replay=0;for(Eigen::Index k=0;k<l;++k)replay+=double(kx[k])*double(g[k]);
    if(std::bit_cast<std::uint64_t>(replay)!=std::bit_cast<std::uint64_t>(double(s[6])))throw std::runtime_error("native row replay mismatch");
    Eigen::MatrixXd nativeP=pinv.cast<double>();Eigen::VectorXd nativeA=a.cast<double>();
    Eigen::VectorXd replayG=nativeP*nativeA;
    for(Eigen::Index k=0;k<l;++k)if(std::bit_cast<std::uint64_t>(replayG[k])!=std::bit_cast<std::uint64_t>(double(g[k])))throw std::runtime_error("native g replay mismatch");
    emit("native_replay_bitwise",1);
    emit("native_g_max_abs",g.cwiseAbs().maxCoeff());emit("native_pinv_max_abs",pinv.cwiseAbs().maxCoeff());
    emit("native_g_extended_dot",kx.dot(g));
    V inverse(l);for(Eigen::Index k=0;k<l;++k)inverse[k]=mask[k]?1/d[k]:0;
    V frozen=u*(inverse.array()*(u.transpose()*a).array()).matrix();
    emit("native_factors_extended_sum",kx.dot(frozen));
    emit("native_eigen_relative_residual",(w*u-u*d.asDiagonal()).norm()/w.norm());
    spectrum("native",d);
    E native(w);emit("native_gram_extended_eigen_sum",kx.dot(apply(native,a)));spectrum("native_gram_extended",native.eigenvalues());
    M rebuilt=gram(z,sigma2);E extended(rebuilt);V kr=row(x,z,sigma2),ar=V::Zero(l),q=apply(extended,kr);
    emit("gram_reconstruction_max_abs",(w-rebuilt).cwiseAbs().maxCoeff());
    emit("kernel_row_reconstruction_max_abs",(kx-kr).cwiseAbs().maxCoeff());
    emit("target_kernel_zero_count",(kx.array()==0).count());emit("source_aggregate_zero_count",(a.array()==0).count());
    emit("target_nearest_landmark_sigma",std::sqrt(-2*std::log(kr.maxCoeff())));
    emit("target_projection_residual",1-kr.dot(q));spectrum("rebuilt",extended.eigenvalues());
    std::size_t maskChanges=0;for(Eigen::Index k=0;k<l;++k) maskChanges+=(extended.eigenvalues()[k]>cutoff*extended.eigenvalues().maxCoeff())!=(mask[k]!=0);
    emit("rebuilt_sorted_mask_changes",maskChanges);
    std::vector<R> logs(m),approx(m),near(m);R top=-std::numeric_limits<R>::infinity(),positive=0,negative=0,absolute=0;
    std::size_t negativeCount=0,kernelBelowNativeCutoff=0;V ky(l);
    for(Eigen::Index i=0;i<m;++i) {
        logs[i]=-(x.row(0)-y.row(i)).squaredNorm()/(2*sigma2)+lw[i];top=std::max(top,logs[i]);
        R best=std::numeric_limits<R>::infinity();
        for(Eigen::Index k=0;k<l;++k) {R d2=(z.row(k)-y.row(i)).squaredNorm();best=std::min(best,d2);R exponent=-d2/(2*sigma2);kernelBelowNativeCutoff+=exponent< -708;ky[k]=std::exp(exponent);}
        R weight=std::exp(lw[i]);ar+=weight*ky;approx[i]=weight*q.dot(ky);near[i]=std::sqrt(best/sigma2);
        if(approx[i]<0){negative+=approx[i];++negativeCount;}else positive+=approx[i];absolute+=std::abs(approx[i]);
    }
    R sum=0;for(R v:logs)sum+=std::exp(v-top);R logsum=top+std::log(sum),logA=logsum+s[2];
    emit("exact_log_sum_shifted",logsum);emit("exact_sum_shifted",std::exp(logsum));emit("exact_log_affinity_unshifted",logA);
    R difference=s[1]-logA;R inlier=difference>0?std::exp(-difference)/(1+std::exp(-difference)):1/(1+std::exp(difference));
    emit("exact_target_inlier_mass",inlier);emit("log_outlier_component_shifted",s[1]-s[2]);
    V matchedInverse(l);for(Eigen::Index k=0;k<l;++k)matchedInverse[k]=mask[k]?1/extended.eigenvalues()[k]:0;
    V left=extended.eigenvectors().transpose()*kr,right=extended.eigenvectors().transpose()*ar;
    V terms=left.array()*right.array()*inverse_values(extended.eigenvalues()).array();
    V matchedTerms=left.array()*right.array()*matchedInverse.array();
    emit("rebuilt_matched_rank_sum",matchedTerms.sum());
    emit("rebuilt_component_abs_sum",terms.cwiseAbs().sum());
    emit("rebuilt_accumulation_scale",256*std::numeric_limits<R>::epsilon()*terms.cwiseAbs().sum());
    emit("matched_accumulation_scale",256*std::numeric_limits<R>::epsilon()*matchedTerms.cwiseAbs().sum());
    std::ofstream components(std::string(argv[2])+".components.csv");components<<"index,native_factors,rebuilt,rebuilt_matched_rank\n"<<std::setprecision(21);
    V nativeTerms=(u.transpose()*kx).array()*(u.transpose()*a).array()*inverse.array();
    for(Eigen::Index k=0;k<l;++k)components<<k<<','<<nativeTerms[k]<<','<<terms[k]<<','<<matchedTerms[k]<<'\n';
    components.close();if(!components)throw std::runtime_error("component write failed");
    emit("rebuilt_sum",kr.dot(apply(extended,ar)));emit("rebuilt_source_contribution_sum",positive+negative);
    emit("rebuilt_positive_mass",positive);emit("rebuilt_negative_mass",negative);emit("rebuilt_negative_source_count",negativeCount);
    emit("rebuilt_cancellation_ratio",absolute/std::max(std::abs(positive+negative),std::numeric_limits<R>::min()));
    emit("source_kernel_below_native_cutoff_count",kernelBelowNativeCutoff);emit("source_aggregate_reconstruction_max_abs",(a-ar).cwiseAbs().maxCoeff());
    std::ofstream sources(std::string(argv[2])+".sources.bin",std::ios::binary);
    for(Eigen::Index i=0;i<m;++i){double v[]{double(logs[i]),double(approx[i]),double(near[i])};sources.write(reinterpret_cast<const char*>(v),sizeof(v));}
    std::ofstream spectra(std::string(argv[2])+".spectrum.csv");spectra<<"index,native,native_gram_extended,rebuilt,retained\n"<<std::setprecision(21);
    for(Eigen::Index k=0;k<l;++k)spectra<<k<<','<<d[k]<<','<<native.eigenvalues()[k]<<','<<extended.eigenvalues()[k]<<','<<mask[k]<<'\n';
    std::vector<Eigen::Index> order(m);std::iota(order.begin(),order.end(),0);std::partial_sort(order.begin(),order.begin()+std::min<Eigen::Index>(8,m),order.end(),[&](auto i,auto j){return logs[i]>logs[j]||(logs[i]==logs[j]&&i<j);});
    std::ofstream tops(std::string(argv[2])+".top.csv");tops<<"source_index,exact_mass_share,nearest_landmark_sigma,projection_residual\n"<<std::setprecision(21);
    for(Eigen::Index k=0;k<std::min<Eigen::Index>(8,m);++k){auto i=order[k];M yi=y.row(i);V ky2=row(yi,z,sigma2);tops<<i<<','<<std::exp(logs[i]-logsum)<<','<<near[i]<<','<<1-ky2.dot(apply(extended,ky2))<<'\n';}
    sources.close();spectra.close();tops.close();if(!sources||!spectra||!tops)throw std::runtime_error("analysis write failed");
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
