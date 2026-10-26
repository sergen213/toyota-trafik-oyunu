#include "game/rules.hpp"

#include <algorithm>
#include <format>

namespace tto {

int Rules::points(Event e) {
    switch (e) {
        case Event::GreenPass: return 2;
        case Event::SignalOk: return 2;
        case Event::RedLight: return -10;
        case Event::YellowLight: return -3;
        case Event::JunctionFast: return -3;
        case Event::Speeding: return -9;
        case Event::SignalError: return -3;
        case Event::CarCollision: return -15;
        case Event::PedestrianHit: return -17;
        case Event::WrongWay: return -10;
        case Event::SchoolBus: return -7;
        case Event::Tailgating: return -2;
        case Event::RightOfWayRight:
        case Event::RightOfWayAhead:
        case Event::RightOfWayLeft: return -4;
        case Event::HornSpam: return -3;
        case Event::HornHospital: return -5;
    }
    return 0;
}

std::string Rules::message(Event e) {
    const auto rw = [](const char* from) {
        return std::string("Kavşak Kuralını İhlal Ettin\n") + from + " Gelen Aracın Geçmesi Gerekirdi";
    };
    std::string t;
    switch (e) {
        case Event::GreenPass:
        case Event::SignalOk: return {};  // bonuses are silent
        case Event::RedLight: t = "Kırmızı Işıkta Geçtin"; break;
        case Event::YellowLight: t = "Sarı Işıkta Geçtin"; break;
        case Event::JunctionFast: t = "Kavşağa Hızlı Girdin"; break;
        case Event::Speeding: t = "Hız Sınırlamasını Aştın"; break;
        case Event::SignalError: t = "Sinyal Hatası Yaptın"; break;
        case Event::CarCollision:
        case Event::PedestrianHit: t = "Daha Dikkatli Ol"; break;
        case Event::WrongWay: t = "Ters Yöne Girdin"; break;
        case Event::SchoolBus: t = "Öğrenci İndiren Okul Taşıtını Solladın"; break;
        case Event::Tailgating: t = "Öndeki Araca Çok Yakınsın"; break;
        case Event::RightOfWayRight: t = rw("Sağdan"); break;
        case Event::RightOfWayAhead: t = rw("Karşıdan"); break;
        case Event::RightOfWayLeft: t = rw("Soldan"); break;
        case Event::HornSpam: t = "Nedensiz Yere Korna Çalıyorsun"; break;
        case Event::HornHospital: t = "Hastanenin Yanından Geçerken Korna Çaldın"; break;
    }
    return std::format("{} ({})", t, points(e));
}

void Rules::reset_attempt() {
    const int keepBus = schoolBus;  // 0x54b1b0 is never reset by the original
    *this = Rules{};
    schoolBus = keepBus;
}

void Rules::push(std::string text, double now) {
    if (text.empty()) return;
    msgs_.push_back({std::move(text), now + kMessageSeconds});
}

void Rules::expire(double now) {
    std::erase_if(msgs_, [now](const Message& m) { return m.until <= now; });
}

void Rules::event(Event e, double now) {
    score_ += points(e);
    switch (e) {
        case Event::GreenPass: ++green; break;
        case Event::SignalOk: ++signalOk; break;
        case Event::RedLight: ++red; break;
        case Event::YellowLight: ++yellow; break;
        case Event::JunctionFast: ++junctionFast; break;
        case Event::Speeding: ++speeding; break;
        case Event::SignalError: ++signalErr; break;
        case Event::CarCollision: ++carHit; break;
        case Event::PedestrianHit: ++pedHit; break;
        case Event::WrongWay: ++wrongWay; break;
        case Event::SchoolBus: ++schoolBus; break;
        case Event::Tailgating: ++tailgate; break;
        default: break;  // right-of-way and horn penalties have no report line
    }
    push(message(e), now);
}

void Rules::sign_answer(bool correct, int group, double now) {
    (void)now;
    if (correct) {
        score_ += 20 * group;
        ++signRight;
    } else {
        score_ -= 10;
        ++signWrong;
    }
}

void Rules::violator_answer(bool correct, Violator v, double now) {
    (void)now;
    const int pts = v == RanRedLight ? 100 : 60;
    if (correct) score_ += pts;
    else score_ -= 10;
    if (v >= 1 && v <= 3) (correct ? violRight : violWrong)[v]++;
}

std::string Rules::report(int total) const {
    const int vp = violRight[1] + violRight[2] + violRight[3];
    const int vm = violWrong[1] + violWrong[2] + violWrong[3];
    std::string s;
    s += std::format("PERFORMANSIN\n TOPLAM PUAN: {}\n\nDOĞRULARIN:\n", total);
    s += std::format("   Yeşil Işıkta Geçme: {}\n   Doğru Sinyal Verme: {}\n", green, signalOk);
    s += std::format("   Hatalı Davranan Araç Yakalama: {}/{}\n", vp, vp + vm);
    s += std::format("   Tabelalara Doğru Yanıt Verme: {}/{}\n\nHATALARIN:\n", signRight, signRight + signWrong);
    s += std::format("   Kırmızı Işıkta Geçme: {}\n   Sarı Işıkta Geçme: {}\n", red, yellow);
    s += std::format("   Kavşağa Hızlı Girme: {}\n   Hız Sınırı İhlal Etme: {}\n", junctionFast, speeding);
    s += std::format("   Yanlış Sinyal Verme: {}\n   Araca Çarpma: {}\n", signalErr, carHit);
    s += std::format("   Yayaya Çarpma: {}\n   Ters Yöne Girme: {}\n", pedHit, wrongWay);
    s += std::format("   Okul Taşıtını Sollama: {}\n   Öndeki Araca Çok Yaklaşma: {}\n", schoolBus, tailgate);
    s += std::format("   Hatalı Davranan Aracı Yanlış Yakalama: {}/{}\n", vm, vp + vm);
    s += std::format("   Tabelalara Yanlış Yanıt Verme: {}/{}\n\n", signWrong, signRight + signWrong);
    s += "   Devam Etmek İçin [ENTER] Tuşuna Basın...";
    return s;
}

}  // namespace tto
