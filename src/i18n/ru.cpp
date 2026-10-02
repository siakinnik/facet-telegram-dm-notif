// Russian translation of the telegram-dm-notif plugin. Keys are the English source strings.
#include "i18n/i18n.h"

namespace tg {

namespace {

const facet::i18n::Table& ru() {
    static const facet::i18n::Table table = {
        // Tile
        {"Not signed in", "Вход не выполнен"},
        {"No new messages", "Нет новых сообщений"},
        {"New: {} · {}", "Новых: {} · {}"},

        // Message list
        {"Connection", "Связь"},
        {"waiting for network", "нет сети"},
        {"updating…", "обновление…"},
        {"connecting…", "подключение…"},
        {"New messages", "Новые сообщения"},
        {"No unread messages in private chats.", "В личных чатах нет непрочитанных сообщений."},
        {"And {} more chats.", "И ещё чатов: {}."},
        {"You replied last", "Последним ответили вы"},
        {"New message", "Новое сообщение"},
        {"{} new · {}", "Новых: {} · {}"},
        {"Photo", "Фото"},
        {"Video", "Видео"},
        {"GIF", "GIF"},
        {"Voice message", "Голосовое сообщение"},
        {"Video message", "Видеосообщение"},
        {"Audio", "Аудио"},
        {"File", "Файл"},
        {"Sticker", "Стикер"},
        {"Location", "Геопозиция"},
        {"Contact", "Контакт"},
        {"Poll", "Опрос"},
        {"Call", "Звонок"},
        {"Video call", "Видеозвонок"},
        {"Dice", "Кубик"},
        {"Story", "История"},
        {"Gift", "Подарок"},
        {"Joined Telegram", "Теперь в Telegram"},

        // Settings and account
        {"Settings", "Настройки"},
        {"Show message text", "Показывать текст сообщений"},
        {"Hide muted chats", "Скрывать чаты без звука"},
        {"Include bots", "Показывать ботов"},
        {"Account", "Аккаунт"},
        {"Signed in as", "Вход выполнен"},
        {"Sign out", "Выйти"},
        {"Tap again to sign out", "Нажмите ещё раз, чтобы выйти"},
        {"Read only: Facet never sends messages and never marks them as read. A chat disappears "
         "from this list once you read it in any Telegram app.",
         "Только чтение: Facet ничего не отправляет и не отмечает сообщения прочитанными. Чат исчезает "
         "из списка, когда вы прочитаете его в любом приложении Telegram."},

        // Setup
        {"Connect Telegram", "Подключение Telegram"},
        {"Facet signs in to Telegram as a regular client app, like Telegram Desktop. Telegram "
         "asks every such app for its own API ID. Open my.telegram.org on a phone or computer, "
         "sign in, choose “API development tools”, create an app with any name and enter its "
         "api_id and api_hash here. They stay on this device.",
         "Facet входит в Telegram как обычное приложение-клиент, как Telegram Desktop. Для каждого "
         "такого приложения Telegram требует свой API ID. Откройте my.telegram.org на телефоне или "
         "компьютере, войдите, выберите «API development tools», создайте приложение с любым названием "
         "и введите здесь его api_id и api_hash. Они хранятся только на этом устройстве."},
        {"API ID", "API ID"},
        {"API hash", "API hash"},
        {"Continue", "Продолжить"},
        {"Check the API ID (a number) and the API hash (32 characters).",
         "Проверьте API ID (число) и API hash (32 символа)."},

        // Sign-in
        {"Sign in", "Вход"},
        {"Telegram", "Telegram"},
        {"starting…", "запуск…"},
        {"signing out…", "выход…"},
        {"please wait…", "подождите…"},
        {"The quickest way: scan a QR code with Telegram on your phone.",
         "Быстрее всего — отсканировать QR-код в Telegram на телефоне."},
        {"Sign in with a QR code", "Войти по QR-коду"},
        {"Phone number", "Номер телефона"},
        {"country code and number", "код страны и номер"},
        {"Send code", "Получить код"},
        {"Change API ID", "Сменить API ID"},
        {"On your phone open Telegram > Settings > Devices > Link Desktop Device and point the "
         "camera at this code.",
         "На телефоне откройте Telegram > Настройки > Устройства > Подключить устройство и наведите "
         "камеру на этот код."},
        {"Use the phone number instead", "Войти по номеру телефона"},
        {"Telegram sent a code to your other devices.", "Telegram отправил код на другие ваши устройства."},
        {"A code was sent by SMS.", "Код отправлен по SMS."},
        {"Telegram sent you a code.", "Telegram отправил вам код."},
        {"Code", "Код"},
        {"Start over", "Начать заново"},
        {"The account has two-step verification. Enter its password.",
         "В аккаунте включена двухэтапная проверка. Введите пароль."},
        {"Hint", "Подсказка"},
        {"Password", "Пароль"},
        {"There is no Telegram account with this number. Create it in the Telegram app first.",
         "Аккаунта Telegram с этим номером нет. Сначала создайте его в приложении Telegram."},
        {"Telegram asks for a step Facet cannot do. Sign in once in the official app, then try "
         "again.",
         "Telegram просит шаг, который Facet не умеет. Войдите один раз в официальном приложении и "
         "попробуйте снова."},

        // Errors
        {"Error", "Ошибка"},
        {"Enter the full number with the country code.", "Введите полный номер с кодом страны."},
        {"This phone number is not valid.", "Неверный номер телефона."},
        {"Wrong code.", "Неверный код."},
        {"The code has expired. Start over.", "Срок действия кода истёк. Начните заново."},
        {"Wrong password.", "Неверный пароль."},
        {"Telegram rejected the API ID. Check it on my.telegram.org.",
         "Telegram не принял API ID. Проверьте его на my.telegram.org."},
        {"Too many attempts. Try again later.", "Слишком много попыток. Попробуйте позже."},
    };
    return table;
}

}  // namespace

void register_translations(facet::i18n::Catalog& catalog) { catalog.add("ru", ru()); }

}  // namespace tg
