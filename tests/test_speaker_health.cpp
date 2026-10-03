#include "SpeakerHealth.h"
#include <cassert>
int main(){SpeakerHealth h;assert(h.state==SpeakerHealth::Unknown);
h.found(SpeakerHealth::Paused);assert(h.state==SpeakerHealth::Paused);
h.missing();assert(h.state!=SpeakerHealth::Unavailable);h.uncertain();h.missing();assert(h.state!=SpeakerHealth::Unavailable);
h.missing();assert(h.state==SpeakerHealth::Unavailable);h.uncertain();assert(h.state==SpeakerHealth::Unavailable);
h.found();assert(h.state==SpeakerHealth::Available);h.found(SpeakerHealth::Playing);h.uncertain();assert(h.state==SpeakerHealth::Unknown);
}
