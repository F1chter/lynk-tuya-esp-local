#include <Arduino.h>
#include <FastBot2.h>

FastBot2 bot;
bool needToSendHello = true;
void (*_sendStatusFunction)() = nullptr;
void (*_rerunFunction)() = nullptr;
void (*_skipChargeFunction)(bool) = nullptr;
void (*_skipRiverFunction)(bool) = nullptr;
void (*_skipHeatersFunction)(bool) = nullptr;


void updateh(fb::Update& u) {
  if (!u.isMessage()) return;
  if (u.message().chat().id() == ADMIN_CHAT_ID) {
    if (u.message().hasDocument()) {
      if (u.message().document().name().endsWith(".bin")) {  // .bin == ОТА
        bot.sendMessage(fb::Message("OTA begin", u.message().chat().id()));
        bot.updateFlash(u.message().document(), u.message().chat().id());
      }
    }
  } else if (u.message().chat().id() == GROUP_CHAT_ID) {
    Serial.println("NEW MESSAGE id/username/text");
    Serial.println(u.message().chat().id());
    Serial.println(u.message().from().username());
    Serial.println(u.message().text());
    if(u.message().text().startsWith("/status") && _sendStatusFunction != nullptr) _sendStatusFunction();
    else if(u.message().text().startsWith("/rerun") && _rerunFunction != nullptr) _rerunFunction();
    else if(u.message().text().startsWith("/skipcharge") && _skipChargeFunction != nullptr) _skipChargeFunction(true);
    else if(u.message().text().startsWith("/skipriver") && _skipRiverFunction != nullptr) _skipRiverFunction(true);
    else if(u.message().text().startsWith("/skipheaters") && _skipHeatersFunction != nullptr) _skipHeatersFunction(true);
    else if(u.message().text().startsWith("/docharge") && _skipChargeFunction != nullptr) _skipChargeFunction(false);
    else if(u.message().text().startsWith("/doriver") && _skipRiverFunction != nullptr) _skipRiverFunction(false);
    else if(u.message().text().startsWith("/doheaters") && _skipHeatersFunction != nullptr) _skipHeatersFunction(false);
  }
}

void setupTelegram() {
  bot.attachUpdate(updateh);  // подключить обработчик обновлений
  bot.setToken(BOT_TOKEN);    // установить токен
  bot.skipUpdates();
}

void tickTelegram() {
  if (needToSendHello) {
    needToSendHello = false;
    bot.sendMessage(fb::Message("ESP Started(waiting OTA...)", ADMIN_CHAT_ID));
    bot.sendMessage(fb::Message("Світло з'явилось. Запускаю сценарій", GROUP_CHAT_ID));
  }
  bot.tick();
}


void sendToChat(String msg) {
  bot.sendMessage(fb::Message(msg, GROUP_CHAT_ID));
}

void sendToAdmin(String msg) {
  bot.sendMessage(fb::Message(msg, ADMIN_CHAT_ID));
}