#ifndef NINJAMJAMROOMCONTROLLER_H
#define NINJAMJAMROOMCONTROLLER_H

#include <QObject>
#include "audio/MetronomeTrackNode.h"
#include <QMap>
//#include "../audio/NinjamTrackNode.h"

namespace Ninjam {
class User;
class UserChannel;
class Server;
}

class NinjamTrackNode;

namespace Controller {

class MainController;


class NinjamJamRoomController : public QObject
{
    Q_OBJECT

public:
    NinjamJamRoomController(Controller::MainController* mainController);
    ~NinjamJamRoomController();
    void process(Audio::SamplesBuffer& in, Audio::SamplesBuffer& out);
    void start(const Ninjam::Server& server);
    void stop();
    bool inline isRunning() const{return running;}
    void setMetronomeBeatsPerAccent(int beatsPerAccent);
    inline int getCurrentBpi() const{return currentBpi;}
    inline int getCurrentBpm() const{return currentBpm;}
signals:
    void currentBpiChanged(int newBpi);
    void currentBpmChanged(int newBpm);
    void intervalBeatChanged(int intervalBeat);
    void channelAdded(const Ninjam::UserChannel& channel, long channelID);
    void channelRemoved(const Ninjam::UserChannel& channel, long channelID);

private:
    Controller::MainController* mainController;
    Audio::MetronomeTrackNode* metronomeTrackNode;
    QMap<Ninjam::UserChannel*, NinjamTrackNode*> trackNodes;


    void addNewTrack(Ninjam::UserChannel *channel);

    bool running;

    long intervalPosition;
    long samplesInInterval;

    int newBpi;
    int newBpm;
    int currentBpi;
    int currentBpm;

    long computeTotalSamplesInInterval();
    long getSamplesPerBeat();

    void processScheduledChanges();
    inline bool hasScheduledChanges() const{return newBpi > 0 || newBpm > 0;}

private slots:
    //ninjam events
    void ninjamServerBpmChanged(short newBpm);
    void ninjamServerBpiChanged(short oldBpi, short newBpi);
    void ninjamAudioAvailable(const Ninjam::User& user, int channelIndex, QByteArray encodedAudioData, bool lastPartOfInterval);
};

}
#endif // NINJAMJAMROOMCONTROLLER_H
