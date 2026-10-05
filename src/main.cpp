#include <SFML/Graphics.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    constexpr float WindowWidth = 800.f;
    constexpr float WindowHeight = 600.f;

    constexpr float PlayerWidth = 32.f;
    constexpr float PlayerHeight = 48.f;

    constexpr float MoveSpeed = 260.f;
    constexpr float Gravity = 1400.f;
    constexpr float JumpSpeed = 600.f;
    constexpr float MaximumFallSpeed = 900.f;

    constexpr float FixedStep = 1.f / 120.f;

    struct Player
    {
        sf::RectangleShape shape;
        sf::Vector2f velocity{ 0.f, 0.f };
        bool grounded = true;

        Player()
        {
            shape.setSize({ PlayerWidth, PlayerHeight });
            shape.setPosition({ 60.f, 512.f });
            shape.setFillColor(sf::Color(80, 210, 255));
        }
    };

    struct Game
    {
        Player player;
        std::vector<sf::RectangleShape> platforms;

        bool paused = false;
        bool jumpRequested = false;

        Game()
        {
            addPlatform(
                { 0.f, 560.f },
                { 800.f, 40.f },
                sf::Color(70, 145, 80));

            addPlatform(
                { 150.f, 450.f },
                { 170.f, 20.f },
                sf::Color(100, 170, 100));

            addPlatform(
                { 360.f, 350.f },
                { 170.f, 20.f },
                sf::Color(110, 180, 110));

            addPlatform(
                { 570.f, 250.f },
                { 150.f, 20.f },
                sf::Color(120, 190, 120));

            addPlatform(
                { 760.f, 400.f },
                { 40.f, 160.f },
                sf::Color(130, 135, 150));
        }

        void addPlatform(
            sf::Vector2f position,
            sf::Vector2f size,
            sf::Color colour)
        {
            sf::RectangleShape platform(size);
            platform.setPosition(position);
            platform.setFillColor(colour);

            platforms.push_back(platform);
        }
    };

    void resetPlayer(Game& game)
    {
        game.player.shape.setPosition({ 60.f, 512.f });
        game.player.velocity = { 0.f, 0.f };
        game.player.grounded = true;

        game.jumpRequested = false;
    }

    void moveHorizontally(Game& game, float dt)
    {
        Player& player = game.player;

        const float movement = player.velocity.x * dt;

        if (movement == 0.f)
            return;

        player.shape.move({ movement, 0.f });

        for (const auto& platform : game.platforms)
        {
            const auto playerBounds =
                player.shape.getGlobalBounds();

            const auto platformBounds =
                platform.getGlobalBounds();

            if (!playerBounds.findIntersection(platformBounds))
                continue;

            auto position = player.shape.getPosition();

            if (movement > 0.f)
            {
                position.x =
                    platformBounds.position.x - PlayerWidth;
            }
            else
            {
                position.x =
                    platformBounds.position.x
                    + platformBounds.size.x;
            }

            player.shape.setPosition(position);
            player.velocity.x = 0.f;
        }

        auto position = player.shape.getPosition();

        position.x = std::clamp(
            position.x,
            0.f,
            WindowWidth - PlayerWidth);

        player.shape.setPosition(position);
    }

    void moveVertically(Game& game, float dt)
    {
        Player& player = game.player;

        player.velocity.y = std::min(
            player.velocity.y + Gravity * dt,
            MaximumFallSpeed);

        const float movement = player.velocity.y * dt;

        player.grounded = false;
        player.shape.move({ 0.f, movement });

        for (const auto& platform : game.platforms)
        {
            const auto playerBounds =
                player.shape.getGlobalBounds();

            const auto platformBounds =
                platform.getGlobalBounds();

            if (!playerBounds.findIntersection(platformBounds))
                continue;

            auto position = player.shape.getPosition();

            if (movement > 0.f)
            {
                // Falling: stand on top of the platform.
                position.y =
                    platformBounds.position.y - PlayerHeight;

                player.grounded = true;
            }
            else if (movement < 0.f)
            {
                // Rising: stop below the platform.
                position.y =
                    platformBounds.position.y
                    + platformBounds.size.y;
            }

            player.shape.setPosition(position);
            player.velocity.y = 0.f;
        }
    }

    void updateGame(Game& game, float dt)
    {
        Player& player = game.player;

        float horizontalInput = 0.f;

        if (sf::Keyboard::isKeyPressed(
            sf::Keyboard::Key::Left))
        {
            horizontalInput -= 1.f;
        }

        if (sf::Keyboard::isKeyPressed(
            sf::Keyboard::Key::Right))
        {
            horizontalInput += 1.f;
        }

        player.velocity.x = horizontalInput * MoveSpeed;

        if (game.jumpRequested && player.grounded)
        {
            player.velocity.y = -JumpSpeed;
            player.grounded = false;
        }

        // Consume the request once, even if the player
        // cannot jump because they are in the air.
        game.jumpRequested = false;

        moveHorizontally(game, dt);
        moveVertically(game, dt);

        // Recovery for future levels containing gaps.
        if (player.shape.getPosition().y > WindowHeight + 100.f)
            resetPlayer(game);
    }

    void updateTitle(
        sf::RenderWindow& window,
        const Game& game)
    {
        std::string title = "PlatformAdventure | ";

        if (game.paused)
        {
            title += "PAUSED - P: Resume";
        }
        else
        {
            title += game.player.grounded ? "Grounded" : "In air";
            title += " | Left/Right: Move | Space: Jump";
            title += " | R: Reset | P: Pause";
        }

        title += " | Escape: Exit";

        window.setTitle(title);
    }

    void drawGame(sf::RenderWindow& window, const Game& game)
    {
        window.clear(sf::Color(25, 35, 60));

        for (const auto& platform : game.platforms)
            window.draw(platform);

        window.draw(game.player.shape);

        if (game.paused)
        {
            sf::RectangleShape overlay(
                { WindowWidth, WindowHeight });

            overlay.setFillColor(sf::Color(0, 0, 0, 130));
            window.draw(overlay);
        }

        window.display();
    }
} // namespace

int main()
{
    sf::RenderWindow window(
        sf::VideoMode({ 800u, 600u }),
        "PlatformAdventure",
        sf::Style::Titlebar | sf::Style::Close);

    window.setVerticalSyncEnabled(true);
    window.setKeyRepeatEnabled(false);

    Game game;

    sf::Clock clock;
    float accumulator = 0.f;

    while (window.isOpen())
    {
        bool resetTiming = false;

        while (const auto event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
                continue;
            }

            if (event->is<sf::Event::FocusLost>())
            {
                game.paused = true;
                game.jumpRequested = false;
                resetTiming = true;
            }

            const auto* key =
                event->getIf<sf::Event::KeyPressed>();

            if (!key)
                continue;

            if (key->code == sf::Keyboard::Key::Escape)
            {
                window.close();
            }
            else if (key->code == sf::Keyboard::Key::P)
            {
                if (game.paused)
                {
                    if (window.hasFocus())
                        game.paused = false;
                }
                else
                {
                    game.paused = true;
                }

                game.jumpRequested = false;
                resetTiming = true;
            }
            else if (key->code == sf::Keyboard::Key::R)
            {
                resetPlayer(game);
                resetTiming = true;
            }
            else if (key->code == sf::Keyboard::Key::Space
                && !game.paused)
            {
                game.jumpRequested = true;
            }
        }

        if (!window.isOpen())
            break;

        float elapsed =
            std::min(clock.restart().asSeconds(), 0.1f);

        if (resetTiming)
        {
            elapsed = 0.f;
            accumulator = 0.f;
        }

        if (game.paused)
        {
            accumulator = 0.f;
        }
        else
        {
            accumulator += elapsed;

            while (accumulator >= FixedStep)
            {
                updateGame(game, FixedStep);
                accumulator -= FixedStep;
            }
        }

        updateTitle(window, game);
        drawGame(window, game);
    }

    return 0;
}
