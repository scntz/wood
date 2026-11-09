// Independent checked C++ workloads. Immutable shared strings keep snapshots
// owning; no dangling views or Wood runtime/container implementation is used.
#include "native_memory.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using Int=std::int64_t;
using Text=std::shared_ptr<const std::string>;
static Int number(const char* text) {
    const std::string input(text); Int value{};
    const auto result=std::from_chars(input.data(),input.data()+input.size(),value);
    if(result.ec!=std::errc{} || result.ptr!=input.data()+input.size()) throw std::runtime_error("invalid integer");
    return value;
}
static Int add(Int a,Int b) {
    if((b>0 && a>INT64_MAX-b) || (b<0 && a<INT64_MIN-b)) throw std::overflow_error("integer overflow");
    return a+b;
}
static std::string dump(const std::vector<std::vector<Text>>& rows) {
    std::size_t size=0;
    for(const auto& row:rows) {
        for(std::size_t i=0;i<row.size();++i) {
            const auto& text=*row.at(i);
            std::size_t bytes=text.size()+(i?1:0);
            if(text.find_first_of(",\r\n\"")!=std::string::npos) {
                if(bytes>SIZE_MAX-2) throw std::length_error("CSV too large");
                bytes+=2;
                for(char ch:text) if(ch=='"') { if(bytes==SIZE_MAX) throw std::length_error("CSV too large"); ++bytes; }
            }
            if(bytes>SIZE_MAX-size) throw std::length_error("CSV too large");
            size+=bytes;
        }
        if(size>SIZE_MAX-2) throw std::length_error("CSV too large");
        size+=2;
    }
    std::string result; result.reserve(size);
    for(const auto& row:rows) {
        for(std::size_t i=0;i<row.size();++i) {
            if(i) result+=',';
            const auto& text=*row.at(i);
            const bool quote=text.find_first_of(",\r\n\"")!=std::string::npos;
            if(quote) result+='"';
            for(char ch:text) { result+=ch; if(quote && ch=='"') result+='"'; }
            if(quote) result+='"';
        }
        result+="\r\n";
    }
    return result;
}
int main(int argc,char** argv) try {
    if(argc!=5) return 2;
    const std::string mode(argv[1]);
    const Int count=number(argv[2]),rounds=number(argv[3]);
    if(count<1 || count>10000 || rounds<1 || rounds>100) return 2;
    if(mode!="list" && mode!="snapshot" && mode!="scalars" && mode!="map" && mode!="pipeline" && mode!="csv" && mode!="array") return 2;
    std::vector<Text> fields;
    const auto changed=std::make_shared<const std::string>("changed");
    for(Int i=0;i<count;++i) {
        std::string value="member-"+std::to_string(i)+":";
        for(int j=0;j<12;++j) value+="payload-";
        fields.push_back(std::make_shared<const std::string>(std::move(value)));
    }
    std::vector<std::vector<Text>> rows;
    for(Int i=0;i<count;++i) { std::vector<Text> row; for(Int j=0;j<16;++j) row.push_back(fields.at(static_cast<std::size_t>((i+j)%count))); rows.push_back(std::move(row)); }
    std::vector<double> numeric;
    for(Int i=0;i<count;++i) numeric.push_back(static_cast<double>(i%17));
    std::vector<Text> result;
    std::vector<Int> scalar;
    std::vector<double> array_result;
    std::string text;
    Int checksum=0;
    bench_reset();
    for(Int round=0;round<rounds;++round) {
        if(mode=="list" || mode=="snapshot") {
            std::vector<Text> values;
            for(const auto& field:fields) values.push_back(field);
            if(mode=="snapshot") {
                const auto snapshot=values;
                values.at(values.size()-1)=changed;
                if(*snapshot.back()!=*fields.back()) throw std::runtime_error("snapshot changed");
            }
            for(const auto& value:values) checksum=add(checksum,static_cast<Int>(value->size()));
            result=std::move(values);
        } else if(mode=="scalars") {
            std::vector<Int> values;
            for(Int i=0;i<count;++i) values.push_back(i);
            for(Int i:values) checksum=add(checksum,i);
            scalar=std::move(values);
        } else if(mode=="map") {
            std::map<Int,Text> table;
            for(Int i=0;i<count;++i) table[i]=fields.at(static_cast<std::size_t>(i));
            for(Int i=0;i<count;++i) checksum=add(checksum,static_cast<Int>(table.at(i)->size()));
            std::vector<Text> values;
            for(Int i=0;i<count;++i) values.push_back(table.at(i));
            result=std::move(values);
        } else if(mode=="pipeline") {
            // Source is pinned for this whole scoped reduction. No cursor value escapes.
            for(const auto& field:fields) checksum=add(checksum,static_cast<Int>(field->size()));
        } else if(mode=="csv") {
            text=dump(rows); checksum=add(checksum,static_cast<Int>(text.size()));
        } else {
            std::vector<double> values(numeric.size());
            double total=0;
            for(std::size_t i=0;i<values.size();++i) { values.at(i)=numeric.at(i)*2.0+1.0; total+=values.at(i); }
            checksum=add(checksum,static_cast<Int>(total)); array_result=std::move(values);
        }
    }
    const auto allocations=bench_stop();
    const auto bytes=bench_bytes(),peak=bench_peak();
    if(mode!="csv") {
        text.clear();
        const auto& strings=mode=="pipeline"?fields:result;
        const std::size_t length=mode=="scalars"?scalar.size():mode=="array"?array_result.size():strings.size();
        for(std::size_t i=0;i<length;++i) {
            if(i) text+='\n';
            text+=mode=="scalars"?std::to_string(scalar.at(i)):mode=="array"?std::to_string(static_cast<Int>(array_result.at(i))):*strings.at(i);
        }
    }
    std::ofstream output(argv[4],std::ios::binary); output.write(text.data(),static_cast<std::streamsize>(text.size()));
    if(!output) return 3;
    std::cout<<"{\"checksum\":"<<checksum<<",\"allocations\":"<<allocations<<",\"requested_bytes\":"<<bytes
             <<",\"peak_additional_bytes\":"<<peak<<",\"retains\":-1,\"releases\":-1,\"unique_managed_slots\":-1,\"shared_managed_slots\":-1}\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
