// Explicit order avoids implementation-dependent dot reduction/contraction.
double pcaDot(dvec3 a,dvec3 b)
{
    precise double value=(a.x*b.x+a.y*b.y)+a.z*b.z;
    return value;
}
dvec3 pcaCross(dvec3 a,dvec3 b)
{
    precise dvec3 value=dvec3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);
    return value;
}
// Port of Geometry.PCA: matching closed form, range reduction and repeated-root fallback.
// Range reduction bounds atan's alternating series by tan(pi/16).
double pcaAtan(double x)
{
    bool reciprocal=x>1.0lf;
    if(reciprocal)x=1.0lf/x;
    for(int i=0;i<2;++i)x=x/(1.0lf+sqrt(1.0lf+x*x));
    precise double square=x*x,term=x,sum=x;
    for(int i=1;i<24;++i){term*=-square;sum+=term/double(2*i+1);}
    sum*=4.0lf;
    return reciprocal?1.57079632679489661923lf-sum:sum;
}
double pcaAcos(double x)
{
    if(x<=-1.0lf)return 3.14159265358979323846lf;
    if(x>=1.0lf)return 0.0lf;
    return 2.0lf*pcaAtan(sqrt((1.0lf-x)/(1.0lf+x)));
}
double pcaCos(double x)
{
    precise double term=1.0lf,sum=term,square=x*x;
    for(int i=1;i<24;++i){term*= -square/double((2*i-1)*(2*i));sum+=term;}
    return sum;
}
// The fallback uses largest-off-diagonal Jacobi rotations with deterministic ties.
// It is needed for repeated roots where independent row cross products lose rank.
void pcaStable(precise dmat3 a, out precise dvec3 values, out precise dmat3 vectors)
{
    vectors=dmat3(1.0lf);
    precise double scale=0.0lf;
    for(int c=0;c<3;++c)for(int r=0;r<3;++r)scale=max(scale,abs(a[c][r]));
    for(int step=0;step<32;++step)
    {
        int p=0,q=1;
        if(abs(a[0][2])>abs(a[p][q])){p=0;q=2;}
        if(abs(a[1][2])>abs(a[p][q])){p=1;q=2;}
        if(abs(a[p][q])<=1e-30lf*scale)break;
        precise double tau=(a[q][q]-a[p][p])/(2.0lf*a[p][q]);
        precise double t=(tau>=0.0lf?1.0lf:-1.0lf)/(abs(tau)+sqrt(1.0lf+tau*tau));
        precise double c=1.0lf/sqrt(1.0lf+t*t),s=t*c,shift=t*a[p][q];
        a[p][p]-=shift;a[q][q]+=shift;a[p][q]=0.0lf;a[q][p]=0.0lf;
        for(int k=0;k<3;++k)
        {
            if(k!=p && k!=q)
            {
                precise double x=a[k][p],y=a[k][q];
                a[k][p]=c*x-s*y;a[p][k]=a[k][p];
                a[k][q]=s*x+c*y;a[q][k]=a[k][q];
            }
            precise double x=vectors[p][k],y=vectors[q][k];
            vectors[p][k]=c*x-s*y;vectors[q][k]=s*x+c*y;
        }
    }
    values=dvec3(max(a[0][0],0.0lf),max(a[1][1],0.0lf),max(a[2][2],0.0lf));
    for(int j=0;j<3;++j)
    {
        int p=j==1?1:0,q=p+1;
        if(values[p]<values[q])
        {
            precise double t=values[p];values[p]=values[q];values[q]=t;
            precise dvec3 v=vectors[p];vectors[p]=vectors[q];vectors[q]=v;
        }
    }
    if(pcaDot(pcaCross(vectors[0],vectors[1]),vectors[2])<0.0lf)vectors[2]=-vectors[2];
}
dvec3 pcaVector(precise dmat3 a, double lambda)
{
    a[0][0]-=lambda;a[1][1]-=lambda;a[2][2]-=lambda;
    precise dvec3 c01=pcaCross(a[0],a[1]),c02=pcaCross(a[0],a[2]),c12=pcaCross(a[1],a[2]);
    precise double l01=pcaDot(c01,c01),l02=pcaDot(c02,c02),l12=pcaDot(c12,c12);
    precise dvec3 best=l01>=l02 && l01>=l12?c01:l02>=l12?c02:c12;
    precise double length=pcaDot(best,best);
    return length>1e-30lf?best/sqrt(length):dvec3(0.0lf);
}
void pcaEigen(double a00,double a01,double a02,double a11,double a12,double a22,out precise dvec3 values,out precise dmat3 vectors)
{
    precise dmat3 a=dmat3(dvec3(a00,a01,a02),dvec3(a01,a11,a12),dvec3(a02,a12,a22));
    precise double mean=(a00+a11+a22)/3.0lf;
    precise double b00=a00-mean,b11=a11-mean,b22=a22-mean;
    precise double p2=(b00*b00+b11*b11+b22*b22+2.0lf*(a01*a01+a02*a02+a12*a12))/6.0lf;
    precise double scale=1.0lf;
    for(int c=0;c<3;++c)for(int r=0;r<3;++r)scale=max(scale,abs(a[c][r]));
    if(!(p2>2.2204460492503131e-16lf*scale*scale))
    {values=dvec3(max(mean,0.0lf));vectors=dmat3(1.0lf);return;}
    precise double p=sqrt(p2),inverse=1.0lf/p;
    precise double c00=b00*inverse,c01=a01*inverse,c02=a02*inverse,c11=b11*inverse,c12=a12*inverse,c22=b22*inverse;
    precise double determinant=c00*c11*c22+2.0lf*c01*c02*c12-c00*c12*c12-c11*c02*c02-c22*c01*c01;
    precise double phi=pcaAcos(max(-1.0lf,min(1.0lf,determinant*0.5lf)))/3.0lf;
    precise double l0=mean+2.0lf*p*pcaCos(phi);
    precise double l2=mean+2.0lf*p*pcaCos(phi+2.09439510239319549231lf);
    precise double l1=3.0lf*mean-l0-l2;
    if(l0>l1){double t=l0;l0=l1;l1=t;}
    if(l1>l2){double t=l1;l1=l2;l2=t;}
    if(l0>l1){double t=l0;l0=l1;l1=t;}
    precise double tolerance=8.0lf*sqrt(2.2204460492503131e-16lf)*max(abs(l0),abs(l2));
    if(l1-l0<=tolerance || l2-l1<=tolerance){pcaStable(a,values,vectors);return;}
    vectors=dmat3(pcaVector(a,l2),pcaVector(a,l1),pcaVector(a,l0));
    for(int i=0;i<3;++i)if(pcaDot(vectors[i],vectors[i])==0.0lf){pcaStable(a,values,vectors);return;}
    precise double projection=pcaDot(vectors[0],vectors[1]);
    vectors[1]-=projection*vectors[0];
    precise double length=sqrt(pcaDot(vectors[1],vectors[1]));
    if(!(length>1e-15lf)){pcaStable(a,values,vectors);return;}
    vectors[1]/=length;
    vectors[2]=pcaCross(vectors[0],vectors[1]);
    length=sqrt(pcaDot(vectors[2],vectors[2]));
    if(!(length>1e-15lf)){pcaStable(a,values,vectors);return;}
    vectors[2]/=length;
    values=dvec3(max(l2,0.0lf),max(l1,0.0lf),max(l0,0.0lf));
}
