#pragma once
#include <string>
#include <vector>

namespace tto {

// Scoring, report counters and timed HUD messages of one map attempt.
// Point values and message texts are those decoded in docs/re/RULES.md §3.4.
class Rules {
public:
    enum class Event {
        GreenPass,       // +2  Yeşil Işıkta Geçme
        SignalOk,        // +2  Doğru Sinyal Verme
        RedLight,        // -10 Kırmızı Işıkta Geçtin
        YellowLight,     // -3  Sarı Işıkta Geçtin
        JunctionFast,    // -3  Kavşağa Hızlı Girdin
        Speeding,        // -9  Hız Sınırlamasını Aştın
        SignalError,     // -3  Sinyal Hatası Yaptın
        CarCollision,    // -15 Daha Dikkatli Ol
        PedestrianHit,   // -17 Daha Dikkatli Ol
        WrongWay,        // -10 Ters Yöne Girdin
        SchoolBus,       // -7  Öğrenci İndiren Okul Taşıtını Solladın
        Tailgating,      // -2  Öndeki Araca Çok Yakınsın
        RightOfWayRight, // -4  Kavşak Kuralını İhlal Ettin (Sağdan ...)
        RightOfWayAhead, // -4  (Karşıdan ...)
        RightOfWayLeft,  // -4  (Soldan ...)
        HornSpam,        // -3  Nedensiz Yere Korna Çalıyorsun
        HornHospital,    // -5  Hastanenin Yanından Geçerken Korna Çaldın
    };
    // Violator kinds as in the original (TAraba+0xb0 codes; 5 = pedestrian).
    enum Violator { RanRedLight = 1, Littering = 2, WrongParking = 3, PedestrianRed = 5 };

    struct Message {
        std::string text;
        double until = 0;
    };

    void reset_attempt();  // score, counters (except the school-bus one, original bug) and messages
    void event(Event e, double now);
    void sign_answer(bool correct, int group, double now);
    void violator_answer(bool correct, Violator v, double now);

    int score() const { return score_; }
    void set_score(int s) { score_ = s; }
    // Report text (GLHUDTextRapor) with `total` = sum of per-map passed scores.
    std::string report(int total) const;
    const std::vector<Message>& messages() const { return msgs_; }
    void expire(double now);
    void notice(std::string text, double now) { push(std::move(text), now); }  // message without points

    static int points(Event e);
    static std::string message(Event e);

    // Counters (report lines).
    int green = 0, signalOk = 0, red = 0, yellow = 0, junctionFast = 0, speeding = 0, signalErr = 0,
        carHit = 0, pedHit = 0, wrongWay = 0, schoolBus = 0, tailgate = 0, signRight = 0, signWrong = 0;
    int violRight[4] = {}, violWrong[4] = {};  // index 1..3 = vehicle codes

    static constexpr double kMessageSeconds = 3.0;  // HUD lifetime: not recovered, chosen

private:
    void push(std::string text, double now);
    int score_ = 0;
    std::vector<Message> msgs_;
};

}  // namespace tto
