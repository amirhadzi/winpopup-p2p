#include "core.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace popup;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
namespace {
int checks=0;
void Check(bool condition,const char* description) {
    if(!condition)throw std::runtime_error(description);
    ++checks;std::cout<<"PASS "<<description<<std::endl;
}
template<class Predicate> bool Wait(Predicate predicate,std::chrono::seconds timeout=30s) {
    auto end=std::chrono::steady_clock::now()+timeout;
    while(std::chrono::steady_clock::now()<end){if(predicate())return true;std::this_thread::sleep_for(10ms);}return false;
}
std::string Read(const fs::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void Write(const fs::path& path,const std::string& bytes){std::ofstream f(path,std::ios::binary);f.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));if(!f)throw std::runtime_error("Cannot create test fixture");}
std::string Payload(size_t length){std::string out(length,'\0');uint32_t state=0x5ACD1873;for(char& byte:out){state^=state<<13;state^=state>>17;state^=state<<5;byte=static_cast<char>(state&255);}return out;}
bool Terminal(FileState state){return state==FileState::Completed||state==FileState::Failed||state==FileState::Cancelled;}
FileTransfer Find(Core& core,uint64_t token){for(auto& t:core.Transfers())if(t.token==token)return t;return {};}
bool NoParts(const fs::path& directory){for(auto& entry:fs::directory_iterator(directory))if(entry.path().filename().wstring().find(L".winpopup-part-")!=std::wstring::npos)return false;return true;}
struct Peers {
    Core a,b;uint32_t ai=0,bi=0;uint64_t lastIncoming=0;fs::path root;
    explicit Peers(fs::path directory):root(std::move(directory)){
        fs::create_directories(root);CoreOptions oa,ob;oa.profilePath=(root/L"sender.tox").wstring();ob.profilePath=(root/L"receiver.tox").wstring();
        oa.name="File sender";ob.name="File receiver";oa.password=ob.password="Disposable file test passphrase";oa.publicNetwork=ob.publicNetwork=false;
        std::string error;Check(a.Start(oa,error),("Sender starts: "+error).c_str());Check(b.Start(ob,error),("Receiver starts: "+error).c_str());
        a.Bootstrap("127.0.0.1",b.UdpPort(),b.DhtKey());b.Bootstrap("127.0.0.1",a.UdpPort(),a.DhtKey());a.AddFriend(b.Address(),"Disposable file transfer integration test");
        std::string request;Check(Wait([&]{for(auto&e:b.Poll())if(e.type==EventType::Request)request=e.key;a.Poll();return !request.empty();},65s),"Real Tox contact request arrives");b.AcceptFriend(request);
        Check(Wait([&]{Pump();auto ac=a.Contacts(),bc=b.Contacts();return ac.size()==1&&bc.size()==1&&ac[0].connection!=Connection::Offline&&bc[0].connection!=Connection::Offline;},65s),"Real Tox peers connect for file tests");
        ai=a.Contacts()[0].number;bi=b.Contacts()[0].number;
    }
    void Pump(){a.Poll();b.Poll();}
    std::pair<uint64_t,uint64_t> Offer(const fs::path& source){
        auto out=a.SendFile(ai,source.wstring());uint64_t in=0;
        if(!Wait([&]{Pump();for(const auto&t:b.Transfers())if(t.direction==FileDirection::Incoming&&t.token>lastIncoming&&t.state==FileState::Offered)in=std::max(in,t.token);return in!=0;})){
            auto failure=Find(a,out);throw std::runtime_error("File offer did not arrive: "+failure.detail);
        }
        lastIncoming=in;return {out,in};
    }
    bool Done(uint64_t out,uint64_t in){return Wait([&]{Pump();return Find(a,out).state==FileState::Completed&&Find(b,in).state==FileState::Completed;},45s);}
};
}
int main(){
    try{
        fs::path root=fs::temp_directory_path()/"WinPopupFileTests"/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        Peers peers(root);auto& a=peers.a;auto& b=peers.b;
        Check(Core::MaxFileBytes()==2ULL*1024*1024*1024&&Core::MaxActiveFiles()==8,"File size and concurrency limits are explicit");
        fs::path binary=root/L"photo-\u4f60\u597d-\u00e9.bin",received=root/L"received-\u4f60\u597d.bin";
        std::string bytes=Payload(2*1024*1024+317);Write(binary,bytes);auto [out,in]=peers.Offer(binary);
        auto offered=Find(b,in);Check(out!=0&&in!=0&&offered.size==bytes.size()&&offered.transferred==0,"Offer has bounded size and stable local transfer tokens");
        Check(offered.publicKey==a.Address().substr(0,64)&&offered.contact==peers.bi,"Incoming file is associated with the authenticated contact key");
        Check(offered.name.find(u8"你好")!=std::string::npos&&offered.path.empty(),"Unicode filename is preserved without trusting a remote destination");
        for(int i=0;i<20;++i){peers.Pump();std::this_thread::sleep_for(10ms);}
        Check(Find(b,in).state==FileState::Offered&&!fs::exists(received)&&NoParts(root),"Offered files create no disk data before explicit acceptance");
        b.AcceptFile(in,received.wstring());
        Check(peers.Done(out,in),"Multi-chunk binary transfer completes at both peers");
        Check(Read(received)==bytes,"Received binary is byte-for-byte identical");
        Check(Find(b,in).transferred==bytes.size()&&Find(a,out).transferred==bytes.size(),"Both peers report the complete byte count");
        Check(Find(b,in).path==received.wstring()&&NoParts(root),"Completed transfer publishes the chosen file and removes its partial file");

        // A complete 1x1 PNG verifies image bytes use the same lossless data path.
        const unsigned char pngBytes[]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,4,0,0,0,181,28,12,2,0,0,0,11,73,68,65,84,120,218,99,100,248,15,0,1,5,1,1,39,24,227,102,0,0,0,0,73,69,78,68,174,66,96,130};
        std::string imageBytes(reinterpret_cast<const char*>(pngBytes),sizeof(pngBytes));Write(root/L"image.png",imageBytes);
        auto image=peers.Offer(root/L"image.png");b.AcceptFile(image.second,(root/L"saved-image.png").wstring());
        Check(peers.Done(image.first,image.second)&&Read(root/L"saved-image.png")==imageBytes,"PNG image transfers unchanged");

        Write(root/L"empty.txt",{});auto empty=peers.Offer(root/L"empty.txt");b.AcceptFile(empty.second,(root/L"saved-empty.txt").wstring());
        Check(peers.Done(empty.first,empty.second)&&fs::file_size(root/L"saved-empty.txt")==0,"Zero-byte files complete correctly");

        auto declined=peers.Offer(root/L"image.png");b.CancelFile(declined.second);
        Check(Wait([&]{peers.Pump();return Find(b,declined.second).state==FileState::Cancelled&&Terminal(Find(a,declined.first).state);}),"Declining an offer cancels the sender and receiver");
        Check(Find(b,declined.second).path.empty()&&NoParts(root),"Declined offer leaves no file or partial data");

        Write(root/L"keep-existing.txt","DO NOT OVERWRITE");auto collision=peers.Offer(root/L"image.png");
        b.AcceptFile(collision.second,(root/L"keep-existing.txt").wstring());
        Check(Wait([&]{peers.Pump();return Find(b,collision.second).state==FileState::Failed;}),"Existing destination is not accepted for replacement");
        Check(Read(root/L"keep-existing.txt")=="DO NOT OVERWRITE"&&NoParts(root),"Existing destination bytes are preserved");
        b.CancelFile(collision.second);a.CancelFile(collision.first);

        auto invalidPath=peers.Offer(root/L"image.png");b.AcceptFile(invalidPath.second,(root/L"missing-folder"/L"file.png").wstring());
        Check(Wait([&]{peers.Pump();return Find(b,invalidPath.second).state==FileState::Failed;}),"Unwritable destination produces a transfer failure");
        Check(NoParts(root),"Unwritable destination leaves no temporary artifact");

        // Queue acceptance and cancellation together to exercise partial cleanup
        // before any chance of a very fast loopback file finishing.
        auto cancelled=peers.Offer(binary);fs::path cancelledPath=root/L"cancelled.bin";
        b.AcceptFile(cancelled.second,cancelledPath.wstring());b.CancelFile(cancelled.second);
        Check(Wait([&]{peers.Pump();return Find(b,cancelled.second).state==FileState::Cancelled&&Terminal(Find(a,cancelled.first).state);}),"Accepted transfer can be cancelled on both peers");
        Check(!fs::exists(cancelledPath)&&NoParts(root),"Cancellation removes the private partial and does not publish incomplete bytes");

        // Both users can cancel at once and immediately start another file.
        // Keep these commands back-to-back: waiting between cancellations can
        // hide stale wire-control messages when a Tox file number is reused.
        for(int attempt=0;attempt<3;++attempt){
            auto simultaneous=peers.Offer(binary);
            a.CancelFile(simultaneous.first);b.CancelFile(simultaneous.second);
            auto afterCancel=peers.Offer(root/L"image.png");
            auto afterPath=root/(L"after-cancel-"+std::to_wstring(attempt)+L".png");
            b.AcceptFile(afterCancel.second,afterPath.wstring());
            Check(peers.Done(afterCancel.first,afterCancel.second)&&Read(afterPath)==imageBytes,
                "Simultaneous cancellation cannot cancel or corrupt the next immediate transfer");
        }

        auto missing=a.SendFile(peers.ai,(root/L"does-not-exist.bin").wstring());
        Check(Wait([&]{peers.Pump();return Find(a,missing).state==FileState::Failed;}),"Missing source produces a failure rather than an offer");
        auto directory=a.SendFile(peers.ai,root.wstring());
        Check(Wait([&]{peers.Pump();return Find(a,directory).state==FileState::Failed;}),"Directory cannot be offered as a file");
        Check(missing!=out&&directory!=missing,"Transfer tokens are not reused after completion or failure");
        size_t beforeMismatch=b.Transfers().size();
        auto mismatched=a.SendFile(peers.ai,(root/L"image.png").wstring(),std::string(64,'0'));
        Check(Wait([&]{peers.Pump();return Find(a,mismatched).state==FileState::Failed;})&&b.Transfers().size()==beforeMismatch,
            "A stale or mismatched contact identity cannot receive a file offer");
        b.AcceptFile(0,(root/L"invalid-token.bin").wstring());b.CancelFile(UINT64_MAX);peers.Pump();
        Check(!fs::exists(root/L"invalid-token.bin")&&Read(received)==bytes,"Unknown transfer controls do not modify completed files");

        // Concurrent transfers exercise independent friend-specific file numbers.
        auto first=peers.Offer(binary),second=peers.Offer(root/L"image.png");
        b.AcceptFile(first.second,(root/L"concurrent.bin").wstring());b.AcceptFile(second.second,(root/L"concurrent.png").wstring());
        Check(peers.Done(first.first,first.second)&&peers.Done(second.first,second.second),"Concurrent independent transfers both finish");
        Check(Read(root/L"concurrent.bin")==bytes&&Read(root/L"concurrent.png")==imageBytes,"Concurrent file chunks are never mixed");

        uint64_t previousIncomingA=0;for(const auto&t:a.Transfers())if(t.direction==FileDirection::Incoming)previousIncomingA=std::max(previousIncomingA,t.token);
        auto reverseOut=b.SendFile(peers.bi,(root/L"image.png").wstring());uint64_t reverseIn=0;
        Check(Wait([&]{peers.Pump();for(const auto&t:a.Transfers())if(t.direction==FileDirection::Incoming&&t.token>previousIncomingA&&t.state==FileState::Offered)reverseIn=t.token;return reverseIn!=0;}),"Receiver can independently offer a file in the reverse direction");
        a.AcceptFile(reverseIn,(root/L"reverse.png").wstring());
        Check(Wait([&]{peers.Pump();return Find(b,reverseOut).state==FileState::Completed&&Find(a,reverseIn).state==FileState::Completed;})&&Read(root/L"reverse.png")==imageBytes,"Reverse transfer preserves the image and confirms both directions");

        std::vector<std::pair<uint64_t,uint64_t>> pending;
        for(size_t i=0;i<Core::MaxActiveFiles();++i)pending.push_back(peers.Offer(root/L"image.png"));
        auto overflow=a.SendFile(peers.ai,(root/L"image.png").wstring());
        Check(Wait([&]{peers.Pump();return Find(a,overflow).state==FileState::Failed;}),"Ninth simultaneous transfer is rejected at the resource limit");
        for(const auto& transfer:pending)b.CancelFile(transfer.second);
        Check(Wait([&]{peers.Pump();return std::all_of(pending.begin(),pending.end(),[&](const auto& t){return Terminal(Find(a,t.first).state)&&Terminal(Find(b,t.second).state);});}),"Cancelling the pending batch releases all transfer slots");

        auto removal=peers.Offer(binary);a.RemoveFriend(peers.ai);
        Check(Wait([&]{peers.Pump();return a.Contacts().empty()&&Terminal(Find(a,removal.first).state);}),"Removing a peer terminates its pending transfer");
        b.CancelFile(removal.second);
        b.Stop();a.Stop();Check(NoParts(root),"Shutdown leaves no private partial files");
        std::cout<<"ALL "<<checks<<" FILE TRANSFER CHECKS PASSED\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
