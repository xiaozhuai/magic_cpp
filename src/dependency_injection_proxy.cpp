//
// Copyright (c) 2025 xiaozhuai
//

#include <concepts>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include "proxy/proxy.h"

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
    template <typename Facade, typename Implementation>
    void bind(Scope scope = Scope::transient) {
        static_assert(pro::proxiable_target<Implementation, Facade>, "Implementation does not satisfy Facade");
        static_assert(std::copy_constructible<pro::proxy<Facade>>, "Injected facade must support copy");

        auto entry = std::make_shared<ProviderEntry<Facade>>();
        entry->scope = scope;
        entry->factory = [](Injector &injector) { return injector.construct<Facade, Implementation>(); };
        providers_[std::type_index(typeid(Facade))] = std::move(entry);
    }

    template <typename Facade, typename Implementation>
    void bind_instance(Implementation value) {
        static_assert(pro::proxiable_target<Implementation, Facade>, "Implementation does not satisfy Facade");
        static_assert(std::copy_constructible<pro::proxy<Facade>>, "Injected facade must support copy");

        auto entry = std::make_shared<ProviderEntry<Facade>>();
        entry->scope = Scope::singleton;
        entry->instance.emplace(pro::make_proxy_shared<Facade, Implementation>(std::move(value)));
        providers_[std::type_index(typeid(Facade))] = std::move(entry);
    }

    template <typename Facade>
    [[nodiscard]] pro::proxy<Facade> get() {
        return resolve<Facade>();
    }

private:
    struct ProviderEntryBase {};

    template <typename Facade>
    struct ProviderEntry : ProviderEntryBase {
        Scope scope = Scope::transient;
        std::function<pro::proxy<Facade>(Injector &)> factory;
        std::optional<pro::proxy<Facade>> instance;
    };

    std::unordered_map<std::type_index, std::shared_ptr<ProviderEntryBase>> providers_;

    template <typename Facade>
    [[nodiscard]] pro::proxy<Facade> resolve() {
        const auto key = std::type_index(typeid(Facade));
        auto it = providers_.find(key);
        if (it == providers_.end()) {
            throw std::runtime_error("Type is not bound in Injector: " + std::string(key.name()));
        }
        auto entry = std::static_pointer_cast<ProviderEntry<Facade>>(it->second);
        if (entry->scope == Scope::singleton) {
            if (!entry->instance) {
                entry->instance.emplace(entry->factory(*this));
            }
            return *entry->instance;
        }
        return entry->factory(*this);
    }

    template <typename Facade, typename Implementation>
    [[nodiscard]] pro::proxy<Facade> construct() {
        return construct_from_tuple<Facade, Implementation, typename InjectionTraits<Implementation>::Dependencies>();
    }

    template <typename Facade, typename Implementation, typename DependencyTuple, std::size_t... Indices>
    [[nodiscard]] pro::proxy<Facade> construct_from_tuple_impl(std::index_sequence<Indices...>) {
        return pro::make_proxy_shared<Facade, Implementation>(get<std::tuple_element_t<Indices, DependencyTuple>>()...);
    }

    template <typename Facade, typename Implementation, typename DependencyTuple>
    [[nodiscard]] pro::proxy<Facade> construct_from_tuple() {
        return construct_from_tuple_impl<Facade, Implementation, DependencyTuple>(
            std::make_index_sequence<std::tuple_size_v<DependencyTuple>>{});
    }
};

//
// Facades
//

enum class LogLevel {
    debug = 0,
    info,
    warning,
    error,
};

PRO_DEF_MEM_DISPATCH(MemLog, log);
struct LoggerFacade                                                                  //
    : pro::facade_builder                                                            //
      ::add_convention<MemLog, void(LogLevel log_level, const std::string &) const>  //
      ::support_copy<pro::constraint_level::nothrow>                                 //
      ::build {};

PRO_DEF_MEM_DISPATCH(MemConnectionString, connection_string);
PRO_DEF_MEM_DISPATCH(MemPoolSize, pool_size);
PRO_DEF_MEM_DISPATCH(MemLogLevel, log_level);
struct AppConfigFacade                                                             //
    : pro::facade_builder                                                          //
      ::add_convention<MemConnectionString, const std::string &() const noexcept>  //
      ::add_convention<MemPoolSize, int() const noexcept>                          //
      ::add_convention<MemLogLevel, LogLevel() const noexcept>                     //
      ::support_copy<pro::constraint_level::nothrow>                               //
      ::build {};

PRO_DEF_MEM_DISPATCH(MemQueryUser, query_user);
struct DatabaseFacade                                         //
    : pro::facade_builder                                     //
      ::add_convention<MemQueryUser, std::string(int) const>  //
      ::support_copy<pro::constraint_level::nothrow>          //
      ::build {};

PRO_DEF_MEM_DISPATCH(MemFindName, find_name);
struct UserRepositoryFacade                                  //
    : pro::facade_builder                                    //
      ::add_convention<MemFindName, std::string(int) const>  //
      ::support_copy<pro::constraint_level::nothrow>         //
      ::build {};

PRO_DEF_MEM_DISPATCH(MemPrintUser, print_user);
struct UserServiceFacade                               //
    : pro::facade_builder                              //
      ::add_convention<MemPrintUser, void(int) const>  //
      ::support_copy<pro::constraint_level::nothrow>   //
      ::build {};

PRO_DEF_MEM_DISPATCH(MemRun, run);
struct ApplicationFacade                              //
    : pro::facade_builder                             //
      ::add_convention<MemRun, void() const>          //
      ::support_copy<pro::constraint_level::nothrow>  //
      ::build {};

//
// Implementations
//

class AppConfig {
public:
    AppConfig(std::string connection_string, int pool_size, LogLevel log_level = LogLevel::info)
        : connection_string_(std::move(connection_string)), pool_size_(pool_size), log_level_(log_level) {}

    [[nodiscard]] const std::string &connection_string() const noexcept { return connection_string_; }

    [[nodiscard]] int pool_size() const noexcept { return pool_size_; }

    [[nodiscard]] LogLevel log_level() const noexcept { return log_level_; }

private:
    std::string connection_string_;
    int pool_size_;
    LogLevel log_level_;
};

class ConsoleLogger {
public:
    explicit ConsoleLogger(pro::proxy<AppConfigFacade> config) : log_level_(config->log_level()) {}

    void log(LogLevel log_level, const std::string &message) const {
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
    using Dependencies = std::tuple<AppConfigFacade>;
};

class Database {
public:
    Database(pro::proxy<LoggerFacade> logger, pro::proxy<AppConfigFacade> config)
        : logger_(std::move(logger)), config_(std::move(config)) {
        logger_->log(LogLevel::debug, "Database created with " + config_->connection_string());
    }

    [[nodiscard]] std::string query_user(int id) const {
        logger_->log(LogLevel::info, "Query user id=" + std::to_string(id));
        return "user#" + std::to_string(id) + "@pool=" + std::to_string(config_->pool_size());
    }

private:
    pro::proxy<LoggerFacade> logger_;
    pro::proxy<AppConfigFacade> config_;
};

template <>
struct InjectionTraits<Database> {
    using Dependencies = std::tuple<LoggerFacade, AppConfigFacade>;
};

class UserRepository {
public:
    explicit UserRepository(pro::proxy<DatabaseFacade> database) : database_(std::move(database)) {}

    [[nodiscard]] std::string find_name(int id) const { return database_->query_user(id); }

private:
    pro::proxy<DatabaseFacade> database_;
};

template <>
struct InjectionTraits<UserRepository> {
    using Dependencies = std::tuple<DatabaseFacade>;
};

class UserService {
public:
    UserService(pro::proxy<UserRepositoryFacade> repository, pro::proxy<LoggerFacade> logger)
        : repository_(std::move(repository)), logger_(std::move(logger)) {}

    void print_user(int id) const {
        logger_->log(LogLevel::info, "UserService composing response");
        std::cout << repository_->find_name(id) << '\n';
    }

private:
    pro::proxy<UserRepositoryFacade> repository_;
    pro::proxy<LoggerFacade> logger_;
};

template <>
struct InjectionTraits<UserService> {
    using Dependencies = std::tuple<UserRepositoryFacade, LoggerFacade>;
};

class Application {
public:
    Application(pro::proxy<UserServiceFacade> service, pro::proxy<LoggerFacade> logger)
        : service_(std::move(service)), logger_(std::move(logger)) {}

    void run() const {
        logger_->log(LogLevel::debug, "Application booted");
        service_->print_user(7);
    }

private:
    pro::proxy<UserServiceFacade> service_;
    pro::proxy<LoggerFacade> logger_;
};

template <>
struct InjectionTraits<Application> {
    using Dependencies = std::tuple<UserServiceFacade, LoggerFacade>;
};

int main() {
    Injector injector;
    injector.bind_instance<AppConfigFacade>(AppConfig{
        "postgres://magic_cpp",
        8,
        LogLevel::debug,
    });
    injector.bind<LoggerFacade, ConsoleLogger>(Scope::singleton);
    injector.bind<DatabaseFacade, Database>(Scope::singleton);
    injector.bind<UserRepositoryFacade, UserRepository>();
    injector.bind<UserServiceFacade, UserService>();
    injector.bind<ApplicationFacade, Application>();

    auto app = injector.get<ApplicationFacade>();
    app->run();
}
