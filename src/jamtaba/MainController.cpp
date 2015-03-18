#include "MainController.h"
#include "audio/core/AudioDriver.h"
#include "midi/portmididriver.h"
#include "audio/core/PortAudioDriver.h"
#include "audio/core/AudioMixer.h"
#include "audio/core/AudioNode.h"
#include "audio/RoomStreamerNode.h"
#include "../loginserver/LoginService.h"
#include "../loginserver/JamRoom.h"
#include "../loginserver/natmap.h"

#include "JamtabaFactory.h"
#include "audio/core/plugins.h"
#include "audio/vst/VstPlugin.h"
#include "audio/vst/vsthost.h"
#include "persistence/ConfigStore.h"
#include "../audio/vst/PluginFinder.h"
//#include "mainframe.h"

#include <QFile>
#include <QDebug>
#include <QApplication>

using namespace Persistence;
using namespace Midi;

namespace Controller {

class AudioListener : public Audio::AudioDriverListenerAdapter{

private:
    MainController* mainController;
public:
    AudioListener(MainController* controller){
        this->mainController = controller;
        //this->streamer = new RoomStreamerNode(QUrl("http://vprjazz.streamguys.net/vprjazz64.mp3"));
        //this->streamer = new RoomStreamerNode(QUrl("http://users.skynet.be/fa046054/home/P22/track56.mp3"));
        //this->streamer = new RoomStreamerNode(QUrl("http://ninbot.com:8000/2050"));
        //this->streamer = new Audio::AudioFileStreamerNode("D:/Documents/Estudos/ComputacaoMusical/Jamtaba2/teste.mp3");
    }

    ~AudioListener(){
        //delete audioMixer;
    }

    virtual void driverStarted(){
        qDebug() << "audio driver started";
    }

    virtual void driverStopped(){
        qDebug() << "audio driver stopped";
    }

    virtual void processCallBack(Audio::SamplesBuffer& in, Audio::SamplesBuffer& out){
        mainController->process(in, out);
    }

    virtual void driverException(const char* msg){
        qDebug() << msg;
    }
};

}//namespace Controller::
//++++++++++++++++++++++++++++++

using namespace Controller;

MainController::MainController(JamtabaFactory* factory, int &argc, char **argv)
    :QApplication(argc, argv),

      audioMixer(new Audio::AudioMixer()),
      roomStreamer(new Audio::RoomStreamerNode()),
      currentStreamRoom(nullptr),
      inputPeaks(0,0),
      roomStreamerPeaks(0,0),
      vstHost(Vst::VstHost::getInstance()),
      pluginFinder(std::unique_ptr<Vst::PluginFinder>(new Vst::PluginFinder()))

{

    setQuitOnLastWindowClosed(false);//wait disconnect from server to close
    configureStyleSheet();

    Login::LoginService* service = factory->createLoginService();
    this->loginService = std::unique_ptr<Login::LoginService>( service );
    this->audioDriver = std::unique_ptr<Audio::AudioDriver>( new Audio::PortAudioDriver(
                ConfigStore::getLastInputDevice(), ConfigStore::getLastOutputDevice(),
                ConfigStore::getFirstAudioInput(), ConfigStore::getLastAudioInput(),
                ConfigStore::getFirstAudioOutput(), ConfigStore::getLastAudioOutput(),
                ConfigStore::getLastSampleRate(), ConfigStore::getLastBufferSize()
                ));
    audioDriverListener = std::unique_ptr<Controller::AudioListener>( new Controller::AudioListener(this));

    midiDriver = new PortMidiDriver();

    QObject::connect(service, SIGNAL(disconnectedFromServer()), this, SLOT(on_disconnectedFromServer()));

    this->audioMixer->addNode( *this->roomStreamer);

    tracksNodes.insert(std::pair<int, Audio::AudioNode*>(1, audioMixer->getLocalInput()));

    vstHost->setSampleRate(audioDriver->getSampleRate());
    vstHost->setBlockSize(audioDriver->getBufferSize());

    //QObject::connect(&*pluginFinder, SIGNAL(scanStarted()), this, SLOT(onPluginScanStarted()));
    //QObject::connect(&*pluginFinder, SIGNAL(scanFinished()), this, SLOT(onPluginScanFinished()));
    QObject::connect(&*pluginFinder, SIGNAL(vstPluginFounded(Audio::PluginDescriptor*)), this, SLOT(onPluginFounded(Audio::PluginDescriptor*)));

    //QString vstDir = "C:/Users/elieser/Desktop/TesteVSTs";
    QString vstDir = "C:/Program Files (x86)/VSTPlugins/";
    pluginFinder->addPathToScan(vstDir.toStdString());
    //scanPlugins();

    qDebug() << "QSetting in " << ConfigStore::getSettingsFilePath();
}

void MainController::initializePluginsList(QStringList paths){
    pluginsDescriptors.clear();
    foreach (QString path, paths) {
        QFile file(path);
        if(file.exists()){
            QString pluginName = Audio::PluginDescriptor::getPluginNameFromPath(path);
            pluginsDescriptors.push_back(new Audio::PluginDescriptor(pluginName, "VST", path));
        }
    }
}

void MainController::scanPlugins(){
    foreach (Audio::PluginDescriptor* descriptor, pluginsDescriptors) {
        delete descriptor;
    }
    pluginsDescriptors.clear();
    ConfigStore::clearVstPaths();
    pluginFinder->scan(vstHost);
}

void MainController::onPluginFounded(Audio::PluginDescriptor* descriptor){
    pluginsDescriptors.push_back(descriptor);
    ConfigStore::addVstPlugin(descriptor->getPath());
}

void MainController::process(Audio::SamplesBuffer &in, Audio::SamplesBuffer &out){
    MidiBuffer midiBuffer = midiDriver->getBuffer();
    vstHost->fillMidiEvents(midiBuffer);//pass midi events to vst host
    audioMixer->process(in, out);

    //output->processReplacing(in, out);
    //out.add(in);

    inputPeaks.left = audioMixer->getLocalInput()->getLastPeakLeft();
    inputPeaks.right = audioMixer->getLocalInput()->getLastPeakRight();

    roomStreamerPeaks.left = roomStreamer->getLastPeak();
    roomStreamerPeaks.right = roomStreamer->getLastPeak();
}

Controller::Peaks MainController::getInputPeaks(){
    return inputPeaks;
}

Peaks MainController::getRoomStreamPeaks(){
    return roomStreamerPeaks;
}

//++++++++++ TRACKS ++++++++++++
void MainController::setTrackPan(int trackID, float pan){
    auto it = tracksNodes.find(trackID);
    if(it != tracksNodes.end()){
        (*it).second->setPan(pan);
    }
}

void MainController::setTrackLevel(int trackID, float level){
    auto it = tracksNodes.find(trackID);
    if(it != tracksNodes.end()){
        (*it).second->setGain( std::pow( level, 4));
    }
}

void MainController::setTrackMute(int trackID, bool muteStatus){
    auto it = tracksNodes.find(trackID);
    if(it != tracksNodes.end()){
        (*it).second->setMuteStatus(muteStatus);
    }
}

bool MainController::trackIsMuted(int trackID) const{
    auto it = tracksNodes.find(trackID);
    if(it != tracksNodes.end()){
        return (*it).second->isMuted();
    }
    return false;
}

//+++++++++++++++++++++++++++++++++

std::vector<Audio::PluginDescriptor*> MainController::getPluginsDescriptors(){
    return pluginsDescriptors;
    //Audio::getPluginsDescriptors(this->vstHost);

//        static std::vector<Audio::PluginDescriptor*> descriptors;
//        descriptors.clear();
//        //+++++++++++++++++
//        descriptors.push_back(new Audio::PluginDescriptor("Delay", "Jamtaba"));

//        Vst::PluginFinder finder;

//        //QString vstDir = "C:/Users/elieser/Desktop/TesteVSTs";
//        QString vstDir = "C:/Program Files (x86)/VSTPlugins/";
//        finder.addPathToScan(vstDir.toStdString());
//        std::vector<Audio::PluginDescriptor*> vstDescriptors = finder.scan(host);
//        //add all vstDescriptors
//        descriptors.insert(descriptors.end(), vstDescriptors.begin(), vstDescriptors.end());

//        //descriptors.push_back(new Audio::PluginDescriptor("OldSkool test", "VST", "C:/Program Files (x86)/VSTPlugins/OldSkoolVerb.dll"));

//        return descriptors;


}

Audio::Plugin* MainController::addPlugin(Audio::PluginDescriptor *descriptor){
    Audio::Plugin* plugin = createPluginInstance(descriptor);
    plugin->start(audioDriver->getSampleRate(), audioDriver->getBufferSize());
    this->audioMixer->getLocalInput()->addProcessor(*plugin);

    return plugin;
}

void MainController::removePlugin(Audio::Plugin *plugin){
    this->audioMixer->getLocalInput()->removeProcessor(*plugin);
}

Audio::Plugin *MainController::createPluginInstance(Audio::PluginDescriptor *descriptor)
{
    if(descriptor->getGroup() == "Jamtaba"){
        if(descriptor->getName() == "Delay"){
            return new Audio::JamtabaDelay(audioDriver->getSampleRate());
        }
    }
    else if(descriptor->getGroup() == "VST"){
        Vst::VstPlugin* vst = new Vst::VstPlugin(this->vstHost);
        if(vst->load(this->vstHost, descriptor->getPath())){
            return vst;
        }
        return nullptr;
    }
    return nullptr;
}

bool MainController::isPlayingRoomStream(){
    return currentStreamRoom != nullptr;
}

Login::AbstractJamRoom* MainController::getCurrentStreamingRoom(){
    return currentStreamRoom;
}

void MainController::on_disconnectedFromServer(){
    exit(0);
}

MainController::~MainController()
{
    this->audioDriver->stop();
    this->midiDriver->stop();

    foreach (Audio::PluginDescriptor* descriptor, pluginsDescriptors) {
        delete descriptor;
    }
    pluginsDescriptors.clear();
}

void MainController::playRoomStream(Login::AbstractJamRoom* room){
    if(room->hasStreamLink()){
        roomStreamer->setStreamPath(room->getStreamLink());
        currentStreamRoom = room;
    }
}

void MainController::stopRoomStream(){
    roomStreamer->stopCurrentStream();
    currentStreamRoom = nullptr;
}

void MainController::start()
{
    audioDriver->addListener(*audioDriverListener);
    audioDriver->start();
    midiDriver->start();

    NatMap map;
    loginService->connectInServer("elieser teste", 0, "channel", map, 0, "teste env", 44100);
}

void MainController::stop()
{
    this->audioDriver->release();
    this->midiDriver->release();
    qDebug() << "disconnecting...";
    loginService->disconnect();
}

Audio::AudioDriver *MainController::getAudioDriver() const
{
    return audioDriver.get();
}

void MainController::configureStyleSheet(){
    QFile styleFile( ":/style/jamtaba.css" );
    if(!styleFile.open( QFile::ReadOnly )){
        qFatal("não carregou estilo!");
    }

    // Apply the loaded stylesheet
    QString style( styleFile.readAll() );
    setStyleSheet( style );
}

Login::LoginService* MainController::getLoginService() const{
    return &*loginService;
}
