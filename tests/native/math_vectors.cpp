// Independent hosted oracle only. Not linked into native Wood outputs.
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <vector>
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    const auto infinity=std::numeric_limits<double>::infinity();
    const auto minimum=std::numeric_limits<double>::denorm_min();
    std::vector<double> cases={0.0,-0.0,minimum,-minimum,
        std::numeric_limits<double>::min(),std::numeric_limits<double>::max(),
        -std::numeric_limits<double>::max()};
    // Every finite exponent, deterministic mantissas and both signs.
    for(int exponent=-1074;exponent<=1023;++exponent) {
        double x=std::ldexp(1.23456789012345,exponent);
        cases.push_back(x); cases.push_back(-x);
    }
    const double pi=0x1.921fb54442d18p+1;
    for(int i=-512;i<=512;++i) {
        double x=i*(pi/4.0);
        cases.push_back(x); cases.push_back(std::nextafter(x,infinity));
        cases.push_back(std::nextafter(x,-infinity));
    }
    std::ofstream output(argv[1],std::ios::binary);
    output<<std::setprecision(17)<<'[';
    bool first=true;
    for(double x:cases) {
        if(!std::isfinite(x)) return 3;
        double s=std::sin(x),c=std::cos(x);
        auto unit=[&](double y){return std::abs(std::nextafter(y,infinity)-y);};
        if(!first) output<<',';
        first=false;
        output<<'['<<x<<','<<s<<','<<c<<','<<unit(s)<<','<<unit(c)<<']';
    }
    output<<"]\n";
    output.close();
    return output?0:4;
}
