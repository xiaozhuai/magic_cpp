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
concept Injectable = requires { typename T::Dependencies; };

class Injector {
public:
    template <typename Interface, typename Implementation = Interface>
    void bind(Scope scope = Scope::transient) {
        static_assert(std::is_base_of_v<Interface, Implementation> || std::is_same_v<Interface, Implementation>,
                      "Implementation must satisfy Interface");

        auto entry = std::make_shared<ProviderEntry>();
        entry->scope = scope;
        entry->factory = [](Injector &injector) -> std::shared_ptr<void> {
            return injector.construct_shared<Implementation>();
        };
        providers_[std::type_index(typeid(Interface))] = std::move(entry);
    }

    template <typename T>
    void bind_instance(T value) {
        auto entry = std::make_shared<ProviderEntry>();
        entry->scope = Scope::singleton;
        entry->instance = std::make_shared<T>(std::move(value));
        entry->factory = [entry](Injector &) -> std::shared_ptr<void> { return entry->instance; };
        providers_[std::type_index(typeid(T))] = std::move(entry);
    }

    template <typename T>
    [[nodiscard]] std::shared_ptr<T> create() {
        return std::static_pointer_cast<T>(resolve(typeid(T)));
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
        if (it != providers_.end()) {
            auto &entry = *it->second;
            if (entry.scope == Scope::singleton) {
                if (!entry.instance) {
                    entry.instance = entry.factory(*this);
                }
                return entry.instance;
            }
            return entry.factory(*this);
        }
        throw std::runtime_error("Type is not bound in Injector: " + std::string(key.name()));
    }

    template <typename T>
    [[nodiscard]] std::shared_ptr<T> construct_shared() {
        if constexpr (!Injectable<T>) {
            return std::make_shared<T>();
        } else {
            return construct_shared_from_tuple<T, typename T::Dependencies>();
        }
    }

    template <typename T, typename DependencyTuple, size_t... Indices>
    [[nodiscard]] std::shared_ptr<T> construct_shared_from_tuple_impl(std::index_sequence<Indices...>) {
        return std::make_shared<T>(create<std::tuple_element_t<Indices, DependencyTuple>>()...);
    }

    template <typename T, typename DependencyTuple>
    [[nodiscard]] std::shared_ptr<T> construct_shared_from_tuple() {
        return construct_shared_from_tuple_impl<T, DependencyTuple>(
            std::make_index_sequence<std::tuple_size_v<DependencyTuple>>{});
    }
};

struct Logger {
    virtual ~Logger() = default;
    virtual void log(const std::string &message) const = 0;
};

class ConsoleLogger : public Logger {
public:
    void log(const std::string &message) const override { std::cout << "[log] " << message << '\n'; }
};

struct AppConfig {
    std::string connection_string;
    int pool_size = 0;
};

class Database {
public:
    using Dependencies = std::tuple<Logger, AppConfig>;

    Database(std::shared_ptr<Logger> logger, std::shared_ptr<AppConfig> config)
        : logger_(std::move(logger)), config_(std::move(config)) {
        logger_->log("Database created with " + config_->connection_string);
    }

    [[nodiscard]] std::string query_user(int id) const {
        logger_->log("Query user id=" + std::to_string(id));
        return "user#" + std::to_string(id) + "@pool=" + std::to_string(config_->pool_size);
    }

private:
    std::shared_ptr<Logger> logger_;
    std::shared_ptr<AppConfig> config_;
};

class UserRepository {
public:
    using Dependencies = std::tuple<Database>;

    explicit UserRepository(std::shared_ptr<Database> database) : database_(std::move(database)) {}

    [[nodiscard]] std::string find_name(int id) const { return database_->query_user(id); }

private:
    std::shared_ptr<Database> database_;
};

class UserService {
public:
    using Dependencies = std::tuple<UserRepository, Logger>;

    UserService(std::shared_ptr<UserRepository> repository, std::shared_ptr<Logger> logger)
        : repository_(std::move(repository)), logger_(std::move(logger)) {}

    void print_user(int id) const {
        logger_->log("UserService composing response");
        std::cout << repository_->find_name(id) << '\n';
    }

private:
    std::shared_ptr<UserRepository> repository_;
    std::shared_ptr<Logger> logger_;
};

class Application {
public:
    using Dependencies = std::tuple<UserService, Logger>;

    Application(std::shared_ptr<UserService> service, std::shared_ptr<Logger> logger)
        : service_(std::move(service)), logger_(std::move(logger)) {}

    void run() const {
        logger_->log("Application booted");
        service_->print_user(7);
    }

private:
    std::shared_ptr<UserService> service_;
    std::shared_ptr<Logger> logger_;
};

int main() {
    Injector injector;
    injector.bind<Logger, ConsoleLogger>(Scope::singleton);
    injector.bind<Database>(Scope::singleton);
    injector.bind<UserRepository>();
    injector.bind<UserService>();
    injector.bind<Application>();
    injector.bind_instance(AppConfig{
        .connection_string = "postgres://magic_cpp",
        .pool_size = 8,
    });

    auto app = injector.create<Application>();
    app->run();

    auto database_a = injector.create<Database>();
    auto database_b = injector.create<Database>();
    auto repo_a = injector.create<UserRepository>();
    auto repo_b = injector.create<UserRepository>();

    std::cout << std::boolalpha;
    std::cout << "Database is singleton: " << (database_a == database_b) << '\n';
    std::cout << "Repository is transient: " << (repo_a != repo_b) << '\n';
    return 0;
}
