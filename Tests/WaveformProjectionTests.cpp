#include "WaveformProjection.h"
#include <cstdlib>
#include <deque>
#include <iostream>
#include <string>

namespace {
int checks = 0;
void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool close(double a, double b) { return std::abs(a-b) <= 2.0e-10 * std::max({1.0,std::abs(a),std::abs(b)}); }

// Independent fixture type: the projection cannot accidentally depend on any
// analyzer implementation, cached RMS, or pre-normalized band colour.
struct Column {
    std::uint64_t endSample = 0;
    int sampleCount = 0;
    std::array<float,2> minimum{}, maximum{};
    std::array<std::uint64_t,2> minimumSample{}, maximumSample{};
    std::array<double,2> sumSquares{};
    std::array<std::array<double,3>,2> bandEnergy{};
    std::array<std::array<std::uint64_t,65>,2> amplitudeCounts{};
};
struct PCM {
    std::array<float,2> audio{};
    std::array<std::array<double,3>,2> filtered{};
};
template<class Signal>
Column summarize(std::uint64_t start, std::uint64_t end, const Signal& signal) {
    Column result;
    result.endSample=end; result.sampleCount=static_cast<int>(end-start);
    result.minimum.fill(std::numeric_limits<float>::infinity());
    result.maximum.fill(-std::numeric_limits<float>::infinity());
    for(auto sample=start; sample<end; ++sample) {
        const auto value=signal(sample);
        for(std::size_t channel=0; channel<2; ++channel) {
            const float audio=value.audio[channel];
            if(audio<result.minimum[channel]){result.minimum[channel]=audio;result.minimumSample[channel]=sample;}
            if(audio>result.maximum[channel]){result.maximum[channel]=audio;result.maximumSample[channel]=sample;}
            result.sumSquares[channel]+=double(audio)*audio;
            const auto bin=static_cast<std::size_t>(std::llround((std::clamp(double(audio),-1.0,1.0)+1.0)*32.0));
            ++result.amplitudeCounts[channel][bin];
            for(std::size_t band=0; band<3; ++band)
                result.bandEnergy[channel][band]+=value.filtered[channel][band]*value.filtered[channel][band];
        }
    }
    return result;
}
void agrees(const WaveformProjectionGroup& group, const Column& raw) {
    require(group.sampleEnd==raw.endSample && group.sampleCount==static_cast<std::uint64_t>(raw.sampleCount),
            "group boundaries and counts match a direct PCM reduction");
    for(std::size_t channel=0; channel<2; ++channel) {
        require(group.minimum[channel]==raw.minimum[channel] && group.maximum[channel]==raw.maximum[channel],
                "signed extrema match the original samples exactly");
        require(group.minimumSample[channel]==raw.minimumSample[channel]
                && group.maximumSample[channel]==raw.maximumSample[channel],
                "extrema retain their original absolute sample positions and earliest ties");
        require(close(group.sumSquares[channel],raw.sumSquares[channel])
                && close(group.rms[channel],std::sqrt(raw.sumSquares[channel]/raw.sampleCount)),
                "RMS is derived from the exact sample-weighted sum of squares");
        require(group.amplitudeCounts[channel]==raw.amplitudeCounts[channel],
                "amplitude occupancy matches a direct count of every original PCM sample");
        std::uint64_t occupancy=0;
        for(auto count:group.amplitudeCounts[channel])occupancy+=count;
        require(occupancy==group.sampleCount,"each amplitude histogram preserves the exact sample count");
        const double total=raw.bandEnergy[channel][0]+raw.bandEnergy[channel][1]+raw.bandEnergy[channel][2];
        for(std::size_t band=0; band<3; ++band)
            require(close(group.bandEnergy[channel][band],raw.bandEnergy[channel][band])
                    && close(group.bandPower[channel][band],total>0?raw.bandEnergy[channel][band]/total:0),
                    "band colours normalize the summed raw filtered energy");
    }
}

void peaksSignsAndTail() {
    const auto signal=[](std::uint64_t sample){
        PCM p;
        p.audio={sample==17?1.2f:-.25f,sample==33?-1.3f:.125f};
        p.filtered[0]={double(p.audio[0]),0,0};p.filtered[1]={0,double(p.audio[1]),0};
        return p;
    };
    std::deque<Column> columns;
    for(std::uint64_t start=0;start<100;start+=10)columns.push_back(summarize(start,start+10,signal));
    columns.push_back(summarize(100,103,signal)); // Actual partial tail, not padding.
    std::vector<WaveformProjectionGroup> groups;
    projectWaveform(columns,103,103,1,10,groups);
    require(groups.size()==1,"a narrow projection combines contiguous completed and observed partial columns");
    agrees(groups[0],summarize(0,103,signal));
    require(groups[0].maximumSample[0]==17 && groups[0].minimumSample[1]==33,
            "isolated positive and negative impulses remain at their exact sample positions");
    require(groups[0].minimum[0]==-.25f && groups[0].maximum[1]==.125f,
            "DC offsets are not replaced with a mirrored symmetric envelope");

    projectWaveform(columns,99.5,100,1,10,groups);
    require(groups.size()==1 && groups[0].sampleEnd==90,
            "future completed columns and partial tails are not drawn ahead of the display cursor");
    projectWaveform(columns,103,18.5,1,10,groups);
    require(groups.front().sampleStart==80 && groups.back().sampleEnd==103,
            "left-edge groups keep their true sample range for drawing-footprint clipping");

    const auto silence=[](std::uint64_t){return PCM{};};
    const std::array<Column,1> silent{{summarize(0,48,silence)}};
    projectWaveform(silent,48,48,10,48,groups);
    agrees(groups[0],silent[0]);
    require(groups[0].rms[0]==0 && groups[0].bandPower[1][2]==0,
            "digital silence has no invented magnitude or band colour energy");

    const auto lastSample=[](std::uint64_t sample){
        PCM p;
        if(sample==52){p.audio={1.4f,-1.6f};p.filtered[0][2]=1.4;p.filtered[1][0]=-1.6;}
        return p;
    };
    const std::array<Column,2> tail{{summarize(0,48,lastSample),summarize(48,53,lastSample)}};
    projectWaveform(tail,52.999,53,1,48,groups);
    require(groups[0].sampleEnd==48 && groups[0].maximum[0]==0,
            "a provisional tail is withheld until its actual exclusive end reaches the cursor");
    projectWaveform(tail,53,53,1,48,groups);
    agrees(groups[0],summarize(0,53,lastSample));
    require(groups[0].maximumSample[0]==52 && groups[0].minimumSample[1]==52,
            "a stopped stream's final partial-bucket impulse survives at its original sample time");
}

void weightsAndGaps() {
    const auto signal=[](std::uint64_t sample){
        PCM p;
        p.audio={sample<2?.8f:.1f,-.4f};
        // Loud raw samples and filtered energy intentionally disagree. Weighting
        // normalized bands by raw RMS produces the wrong result here.
        p.filtered[0]=sample<2?std::array<double,3>{.01,0,0}:std::array<double,3>{0,.7,.3};
        p.filtered[1]={.2,.3,.4};return p;
    };
    std::vector<Column> columns{summarize(0,2,signal),summarize(2,12,signal)};
    std::vector<WaveformProjectionGroup> groups;
    projectWaveform(columns,12,12,1,2,groups);
    agrees(groups[0],summarize(0,12,signal));
    require(groups[0].bandPower[0][0]<.001 && groups[0].bandPower[0][1]>.8,
            "filtered-energy merging is independent of raw waveform loudness");
    require(!close(groups[0].rms[0],(.8+.1)*.5),"unequal bucket lengths do not average RMS values");

    columns={summarize(0,2,signal),summarize(4,6,signal),summarize(6,12,signal)};
    projectWaveform(columns,12,12,1,2,groups);
    require(groups.size()==2 && groups[0].sampleEnd==2 && groups[1].sampleStart==4,
            "a missing sample interval splits a temporal group instead of inventing audio");
    agrees(groups[1],summarize(4,12,signal));
    columns.insert(columns.begin()+1,columns.front()); // Duplicate retained column.
    projectWaveform(columns,12,12,1,2,groups);
    require(groups.size()==2 && groups[0].sampleCount==2,"overlapping columns are not double-counted");

    columns.clear();
    for(std::uint64_t start=0;start<128;start+=4)
        columns.push_back(summarize(start,start+4,signal));
    const auto direct=summarize(0,128,signal);
    for(double width:{4.0,8.0,32.0,256.0}) {
        projectWaveform(columns,128,128,width,4,groups);
        std::array<double,2> squares{};
        std::array<std::array<double,3>,2> energies{};
        std::array<std::array<std::uint64_t,65>,2> amplitudes{};
        std::uint64_t samples=0;
        for(const auto& group:groups) {
            samples+=group.sampleCount;
            for(std::size_t ch=0;ch<2;++ch) {
                squares[ch]+=group.rms[ch]*group.rms[ch]*group.sampleCount;
                for(std::size_t band=0;band<3;++band)energies[ch][band]+=group.bandEnergy[ch][band];
                for(std::size_t bin=0;bin<65;++bin)amplitudes[ch][bin]+=group.amplitudeCounts[ch][bin];
            }
        }
        require(samples==128,"four-pixel and denser projections cover exactly the same PCM");
        for(std::size_t ch=0;ch<2;++ch) {
            require(close(squares[ch],direct.sumSquares[ch]),
                    "changing physical reduction density conserves sample-weighted RMS energy");
            require(amplitudes[ch]==direct.amplitudeCounts[ch],
                    "every physical display density conserves the full amplitude distribution");
            for(std::size_t band=0;band<3;++band)
                require(close(energies[ch][band],direct.bandEnergy[ch][band]),
                        "changing physical reduction density conserves every raw band energy");
        }
    }
}

PCM generated(std::uint64_t sample, std::uint64_t rate) {
    PCM p;
    const bool silent=(sample/(rate*3))%8==5;
    if(silent)return p;
    const float saw=float(int(sample%193)-96)/128.f;
    const float triangle=float(std::abs(int(sample%157)-78)-39)/64.f;
    p.audio={saw*.53f,triangle*.67f};
    if(sample%7919==125)p.audio[0]=1.08f;
    if(sample%6151==841)p.audio[1]=-1.04f;
    for(std::size_t ch=0;ch<2;++ch) {
        const double n=double(int((sample*(ch+3))%59)-29)/128.0;
        p.filtered[ch]={double(p.audio[ch])*.51,n,double(triangle)*(.11+.05*ch)};
    }
    return p;
}

void independentPCMDensities() {
    for(std::uint64_t rate:{44100ull,48000ull,192000ull}) {
        const auto bucket=static_cast<std::uint64_t>(std::llround(double(rate)/1000.0));
        const auto signal=[rate](std::uint64_t s){return generated(s,rate);};
        std::deque<Column> columns;
        const auto last=rate*25+137;
        for(std::uint64_t start=0;start<last;start+=bucket)
            columns.push_back(summarize(start,std::min(last,start+bucket),signal));
        for(double seconds:{6.0,12.0,24.0})for(double width:{240.0,768.0,2048.0}) {
            std::vector<WaveformProjectionGroup> groups, shifted;
            const double head=double(last)-bucket*2-.25;
            const auto info=projectWaveform(columns,head,seconds*rate,width,bucket,groups);
            require(info.gridSamples%bucket==0 && info.gridSamples>=bucket,
                    "projection grid uses complete bucket multiples at every physical pixel density");
            require(!groups.empty() && groups.size()<=static_cast<std::size_t>(width)+2,
                    "sample-preserving projection remains bounded by physical display density");
            for(const auto& group:groups) {
                require(double(group.sampleEnd)<=head,"no future sample interval reaches the display");
                agrees(group,summarize(group.sampleStart,group.sampleEnd,signal));
            }
            const auto moved=projectWaveform(columns,head+bucket*.37,seconds*rate,width,bucket,shifted);
            require(moved.gridSamples==info.gridSamples,"fractional scrolling does not change temporal grid size");
            std::size_t match=0;
            for(const auto& group:groups) {
                while(match<shifted.size()&&shifted[match].sampleStart<group.sampleStart)++match;
                if(match<shifted.size()&&shifted[match].sampleStart==group.sampleStart
                   &&shifted[match].sampleEnd==group.sampleEnd)
                    require(group.minimumSample==shifted[match].minimumSample
                            &&group.maximumSample==shifted[match].maximumSample
                            &&group.sumSquares==shifted[match].sumSquares,
                            "retained groups translate without moving their extrema or recomputing bins");
                if(match<shifted.size()&&shifted[match].sampleStart==group.sampleStart
                   &&shifted[match].sampleEnd==group.sampleEnd)
                    require(group.amplitudeCounts==shifted[match].amplitudeCounts,
                            "fractional scrolling does not redistribute actual amplitude occupancy");
            }
        }
    }
}

void finiteScaling() {
    const auto signal=[](std::uint64_t){PCM p;p.audio={.1f,-.2f};return p;};
    std::vector<Column> columns{summarize(0,48,signal),summarize(48,96,signal)};
    std::vector<WaveformProjectionGroup> groups;
    for(double invalid:{0.0,-1.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        projectWaveform(columns,96,96,invalid,48,groups);
        require(groups.empty(),"invalid physical widths clear projection safely");
        projectWaveform(columns,96,invalid,96,48,groups);
        require(groups.empty(),"invalid time spans clear projection safely");
    }
    const auto info=projectWaveform(columns,96,1.0e308,1.0e-300,48,groups);
    require(std::isfinite(info.samplesPerPixel)&&groups.size()==1,
            "extreme finite scaling cannot overflow grid conversion");
    for(auto& c:columns)for(std::size_t ch=0;ch<2;++ch) {
        c.sumSquares[ch]=std::numeric_limits<double>::max()*.75;
        c.bandEnergy[ch].fill(std::numeric_limits<double>::max()*.75);
    }
    projectWaveform(columns,96,96,1,48,groups);
    require(std::isfinite(groups[0].rms[0])&&close(groups[0].bandPower[0][0],1.0/3.0),
            "huge finite summaries keep RMS and energy normalization finite");
    columns[0].minimum[0]=std::numeric_limits<float>::quiet_NaN();
    projectWaveform(columns,96,96,1,48,groups);
    require(groups.size()==1&&groups[0].sampleStart==48,
            "corrupt source summaries are omitted instead of manufacturing replacement samples");
    projectWaveform(columns,std::numeric_limits<double>::quiet_NaN(),96,1,48,groups);
    require(groups.empty(),"an invalid display cursor produces no geometry");
}

void amplitudeOccupancyConservation() {
    const auto signal=[](std::uint64_t sample){
        PCM p;
        p.audio={0.f,sample%3==0?-.5f:sample%3==1?0.f:.5f};
        return p;
    };
    std::vector<Column> columns;
    constexpr std::uint64_t total=384*300;
    for(std::uint64_t start=0;start<total;start+=384)
        columns.push_back(summarize(start,start+384,signal));
    std::vector<WaveformProjectionGroup> groups;
    projectWaveform(columns,total,total,1,384,groups);
    require(groups.size()==1,"dense occupancy fixture can be reduced to one real temporal group");
    agrees(groups[0],summarize(0,total,signal));
    require(groups[0].amplitudeCounts[0][32]==total && groups[0].amplitudeCounts[0][32]>65535,
            "projection accumulates amplitude occupancy in wide counters without 16-bit bucket overflow");
    require(groups[0].amplitudeCounts[1][16]==total/3 && groups[0].amplitudeCounts[1][32]==total/3
            &&groups[0].amplitudeCounts[1][48]==total/3,
            "independent channel amplitude distributions survive heavy reduction");

    // Only the newest provisional tail belongs in a display snapshot. A repeated
    // tail is ignored as an overlap; replacing it with its completed interval
    // must produce the same count as reducing the source PCM directly.
    columns={summarize(0,48,signal),summarize(48,53,signal),summarize(48,53,signal)};
    projectWaveform(columns,53,53,1,48,groups);
    require(groups.size()==1,"repeated provisional tail does not create another density group");
    agrees(groups[0],summarize(0,53,signal));
    columns={summarize(0,48,signal),summarize(48,96,signal)};
    projectWaveform(columns,96,96,1,48,groups);
    agrees(groups[0],summarize(0,96,signal));

    columns[0].amplitudeCounts[0][32]+=1;
    projectWaveform(columns,96,96,1,48,groups);
    require(groups.size()==1&&groups[0].sampleStart==48,
            "a corrupt histogram that counts more PCM than its interval is rejected");
    columns[0]=summarize(0,48,signal);
    columns[0].amplitudeCounts[0][32]=std::numeric_limits<std::uint64_t>::max();
    projectWaveform(columns,96,96,1,48,groups);
    require(groups.size()==1&&groups[0].sampleCount==48,
            "maliciously large histogram counters cannot overflow sample conservation checks");
}
}

int main() {
    peaksSignsAndTail();weightsAndGaps();independentPCMDensities();finiteScaling();amplitudeOccupancyConservation();
    std::cout << "Waveform projection tests passed (" << checks << " checks)\n";
}
