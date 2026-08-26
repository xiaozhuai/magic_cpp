//
// Copyright (c) 2025 xiaozhuai
//

#include <concepts>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

enum class Scope {
    transient,
    singleton,
};

template <typename T>
struct InjectionTraits {
    using Dependencies = std::tuple<>;
};

class Injector {
public:
    template <typename Interface, typename Implementation = Interface>
    void bind(Scope scope = Scope::transient) {
        static_assert(std::is_base_of_v<Interface, Implementation> || std::is_same_v<Interface, Implementation>,
                      "Implementation must satisfy Interface");

        auto entry = std::make_shared<ProviderEntry>();
        entry->scope = scope;
        entry->factory = [](Injector &injector) -> std::shared_ptr<void> {
            return std::static_pointer_cast<Interface>(injector.construct_shared<Implementation>());
        };
        providers_[std::type_index(typeid(Interface))] = std::move(entry);
    }

    template <typename Interface, typename Implementation>
    void bind_instance(Implementation value) {
        static_assert(std::is_base_of_v<Interface, Implementation> || std::is_same_v<Interface, Implementation>,
                      "Implementation must satisfy Interface");
        auto instance = std::make_shared<Implementation>(std::move(value));
        auto entry = std::make_shared<ProviderEntry>();
        entry->scope = Scope::singleton;
        entry->instance = std::static_pointer_cast<Interface>(std::move(instance));
        providers_[std::type_index(typeid(Interface))] = std::move(entry);
    }

    template <typename T>
    void bind_instance(T value) {
        bind_instance<T, T>(std::move(value));
    }

    template <typename T>
    [[nodiscard]] std::shared_ptr<T> get() {
        return std::static_pointer_cast<T>(resolve(std::type_index(typeid(T))));
    }

private:
    struct ProviderEntry {
        Scope scope = Scope::transient;
        std::function<std::shared_ptr<void>(Injector &)> factory;
        std::shared_ptr<void> instance;
    };

    std::unordered_map<std::type_index, std::shared_ptr<ProviderEntry>> providers_;

    [[nodiscard]] std::shared_ptr<void> resolve(std::type_index key) {
        auto it = providers_.find(key);
        if (it == providers_.end()) {
            throw std::runtime_error("Type is not bound in Injector: " + std::string(key.name()));
        }
        auto &entry = *it->second;
        if (entry.scope == Scope::singleton) {
            if (!entry.instance) {
                entry.instance = entry.factory(*this);
            }
            return entry.instance;
        }
        return entry.factory(*this);
    }

    template <typename T>
    [[nodiscard]] std::shared_ptr<T> construct_shared() {
        return construct_shared_from_tuple<T, typename InjectionTraits<T>::Dependencies>();
    }

    template <typename T, typename DependencyTuple, size_t... Indices>
    [[nodiscard]] std::shared_ptr<T> construct_shared_from_tuple_impl(std::index_sequence<Indices...>) {
        return std::make_shared<T>(get<std::tuple_element_t<Indices, DependencyTuple>>()...);
    }

    template <typename T, typename DependencyTuple>
    [[nodiscard]] std::shared_ptr<T> construct_shared_from_tuple() {
        return construct_shared_from_tuple_impl<T, DependencyTuple>(
            std::make_index_sequence<std::tuple_size_v<DependencyTuple>>{});
    }
};

enum class LogLevel {
    debug = 0,
    info,
    warning,
    error,
};

struct AppConfig {
    std::string connection_string;
    int pool_size = 0;
    LogLevel log_level = LogLevel::info;
};

struct Logger {
    virtual ~Logger() = default;
    virtual void log(LogLevel log_level, const std::string &message) const = 0;
};

class ConsoleLogger : public Logger {
public:
    explicit ConsoleLogger(std::shared_ptr<AppConfig> config) : log_level_(config->log_level) {}

    void log(LogLevel log_level, const std::string &message) const override {
        if (log_level < log_level_) {
            return;
        }
        auto log_level_to_tag = [](LogLevel log_level) {
            switch (log_level) {
                case LogLevel::debug:
                    return "[D]";
                case LogLevel::info:
                    return "[I]";
                case LogLevel::warning:
                    return "[W]";
                case LogLevel::error:
                    return "[E]";
                default:
                    return "[?]";
            }
        };
        std::cout << log_level_to_tag(log_level) << " " << message << '\n';
    }

private:
    LogLevel log_level_ = LogLevel::info;
};

template <>
struct InjectionTraits<ConsoleLogger> {
    using Dependencies = std::tuple<AppConfig>;
};

class Database {
public:
    Database(std::shared_ptr<Logger> logger, std::shared_ptr<AppConfig> config)
        : logger_(std::move(logger)), config_(std::move(config)) {
        logger_->log(LogLevel::debug, "Database created with " + config_->connection_string);
    }

    [[nodiscard]] std::string query_user(int id) const {
        logger_->log(LogLevel::info, "Query user id=" + std::to_string(id));
        return "user#" + std::to_string(id) + "@pool=" + std::to_string(config_->pool_size);
    }

private:
    std::shared_ptr<Logger> logger_;
    std::shared_ptr<AppConfig> config_;
};

template <>
struct InjectionTraits<Database> {
    using Dependencies = std::tuple<Logger, AppConfig>;
};

class UserRepository {
public:
    explicit UserRepository(std::shared_ptr<Database> database) : database_(std::move(database)) {}

    [[nodiscard]] std::string find_name(int id) const { return database_->query_user(id); }

private:
    std::shared_ptr<Database> database_;
};

template <>
struct InjectionTraits<UserRepository> {
    using Dependencies = std::tuple<Database>;
};

class UserService {
public:
    UserService(std::shared_ptr<UserRepository> repository, std::shared_ptr<Logger> logger)
        : repository_(std::move(repository)), logger_(std::move(logger)) {}

    void print_user(int id) const {
        logger_->log(LogLevel::debug, "UserService composing response");
        std::cout << repository_->find_name(id) << '\n';
    }

private:
    std::shared_ptr<UserRepository> repository_;
    std::shared_ptr<Logger> logger_;
};

template <>
struct InjectionTraits<UserService> {
    using Dependencies = std::tuple<UserRepository, Logger>;
};

class Application {
public:
    Application(std::shared_ptr<UserService> service, std::shared_ptr<Logger> logger)
        : service_(std::move(service)), logger_(std::move(logger)) {}

    void run() const {
        logger_->log(LogLevel::debug, "Application booted");
        service_->print_user(7);
    }

private:
    std::shared_ptr<UserService> service_;
    std::shared_ptr<Logger> logger_;
};

template <>
struct InjectionTraits<Application> {
    using Dependencies = std::tuple<UserService, Logger>;
};

int main() {
    Injector injector;
    injector.bind_instance(AppConfig{
        .connection_string = "postgres://magic_cpp",
        .pool_size = 8,
        .log_level = LogLevel::debug,
    });
    injector.bind<Logger, ConsoleLogger>(Scope::singleton);
    injector.bind<Database>(Scope::singleton);
    injector.bind<UserRepository>();
    injector.bind<UserService>();
    injector.bind<Application>();

    auto app = injector.get<Application>();
    app->run();
    return 0;
}
