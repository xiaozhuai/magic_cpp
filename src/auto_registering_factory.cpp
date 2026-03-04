//
// Copyright (c) 2025 xiaozhuai
//

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

template <class Base, typename... Args>
class Factory {
public:
    static Base *create(std::string_view type, Args &&...args) {
        auto obj = create_unique(type, std::forward<Args>(args)...);
        return obj ? obj.release() : nullptr;
    }

    static std::shared_ptr<Base> create_shared(std::string_view type, Args &&...args) {
        auto obj = create_unique(type, std::forward<Args>(args)...);
        return obj ? std::shared_ptr<Base>(std::move(obj)) : nullptr;
    }

    static std::unique_ptr<Base> create_unique(std::string_view type, Args &&...args) {
        auto &map = creators();
        auto it = map.find(type);
        return (it != map.end()) ? (it->second)(std::forward<Args>(args)...) : nullptr;
    }

    template <class Derived>
    struct Registrar final {
        explicit Registrar(std::string_view type) {
            static_assert(std::is_base_of_v<Base, Derived>, "Derived must inherit from Base");
            creators().insert_or_assign(std::string(type), [](Args &&...args) -> std::unique_ptr<Base> {
                return std::make_unique<Derived>(std::forward<Args>(args)...);
            });
        }
    };

private:
    struct StringHash {
        using is_transparent = void;
        [[nodiscard]] size_t operator()(const char *txt) const { return std::hash<std::string_view>{}(txt); }
        [[nodiscard]] size_t operator()(std::string_view txt) const { return std::hash<std::string_view>{}(txt); }
        [[nodiscard]] size_t operator()(const std::string &txt) const { return std::hash<std::string>{}(txt); }
    };
    using StringEqual = std::equal_to<>;

    using Creator = std::function<std::unique_ptr<Base>(Args...)>;
    using CreatorMap = std::unordered_map<std::string, Creator, StringHash, StringEqual>;

    static CreatorMap &creators() {
        static CreatorMap instance;
        return instance;
    }
};

class Animal {
public:
    Animal(std::string name, int age) : name(std::move(name)), age(age) {}
    virtual ~Animal() = default;
    virtual void sound() const = 0;
    void info() const { std::cout << "Name: " << name << ", Age: " << age << std::endl; }

protected:
    std::string name;
    int age = 0;
};

using AnimalFactory = Factory<Animal, std::string, int>;

class Bull : public Animal {
public:
    Bull(std::string name, int age) : Animal(std::move(name), age) {}
    void sound() const override { std::cout << "Moo" << std::endl; }

private:
    [[maybe_unused]] static inline AnimalFactory::Registrar<Bull> registrar{"bull"};
};

class Dog : public Animal {
public:
    Dog(std::string name, int age) : Animal(std::move(name), age) {}
    void sound() const override { std::cout << "Woof" << std::endl; }

private:
    [[maybe_unused]] static inline AnimalFactory::Registrar<Dog> registrar{"dog"};
};

class Cat : public Animal {
public:
    Cat(std::string name, int age) : Animal(std::move(name), age) {}
    void sound() const override { std::cout << "Meow" << std::endl; }

private:
    [[maybe_unused]] static inline AnimalFactory::Registrar<Cat> registrar{"cat"};
};

int main() {
    auto *bull = AnimalFactory::create("bull", "Bessie", 5);
    if (bull) {
        bull->sound();
        bull->info();
    }
    delete bull;

    auto dog = AnimalFactory::create_unique("dog", "Buddy", 3);
    if (dog) {
        dog->sound();
        dog->info();
    }

    auto cat = AnimalFactory::create_shared("cat", "Whiskers", 2);
    if (cat) {
        cat->sound();
        cat->info();
    }
    return 0;
}
