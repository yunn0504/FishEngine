#ifndef GameObject_hpp
#define GameObject_hpp

#include "Transform.hpp"
#include "Script.hpp"
#include "Object.hpp"
#include "Component_gen.hpp"

namespace FishEngine
{
    // Base class for all entities in Unity scenes.
    // http://docs.unity3d.com/ScriptReference/GameObject.html
    class GameObject : public Object
    {
    public:
        //private:
        GameObject(const std::string& name);
        GameObject(const GameObject&) = delete;
        GameObject& operator=(const GameObject&) = delete;

    public:
        ~GameObject() = default;
        // {
        //     Debug::Log("~GameObject()");
        // }

        /************************************************************************/
        /*                            Variables                                 */
        /************************************************************************/

        // Is the GameObject active in the scene ?
        bool activeInHierarchy() const;

        // The local active state of this GameObject. (Read Only)
        bool activeSelf() const
        {
            return m_activeSelf;
        }

        // The layer the game object is in. A layer is in the range [0...31].
        int layer() const
        {
            return m_layer;
        }

        void SetLayer(int layer)
        {
            m_layer = layer;
        }

        // The tag of this game object.
        std::string tag() const
        {
            return m_tag;
        }

        void SetTag(const std::string& tag)
        {
            m_tag = tag;
        }

        // The Transform attached to this GameObject.
        PTransform transform() const
        {
            return m_transform;
        }

        /************************************************************************/
        /*                         Public Functions                             */
        /************************************************************************/

        // Returns the component of Type type if the game object has one attached, null if it doesn't.
        template<typename T>
        std::shared_ptr<T> GetComponent() const
        {
            for (auto& comp : m_components)
            {
                if (comp->ClassName() == T::StaticClassName())
                {
                    return std::static_pointer_cast<T>(comp);
                }
            }
            return nullptr;
        }

        template<typename T>
        std::shared_ptr<T> GetScript() const
        {
            for (auto& s : m_scripts)
            {
                if (s->ClassName() == T::StaticClassName())
                {
                    return std::static_pointer_cast<T>(s);
                }
            }
            return nullptr;
        }


        // Adds a component class named className to the game object.
        template<class T, std::enable_if_t<!std::is_base_of<Script, T>::value, int> = 42>
        bool AddComponent(std::shared_ptr<T> component)
        {
            static_assert(std::is_base_of<Component, T>::value, "Component only");
            if (IsUniqueComponent<T>() && GetComponent<T>() != nullptr)
            {
                return false;
            }
            component->m_gameObject = m_transform->gameObject();
            m_components.push_back(component);
            return true;
        }

        template<class T, std::enable_if_t<std::is_base_of<Script, T>::value, int> = 42>
        bool AddComponent(std::shared_ptr<T> component)
        {
            static_assert(std::is_base_of<Script, T>::value, "Script only");
            component->m_gameObject = m_transform->gameObject();
            m_scripts.push_back(component);
            return true;
        }

        template<class T, std::enable_if_t<!std::is_base_of<Script, T>::value, int> = 42>
        std::shared_ptr<T>
        AddComponent()
        {
            static_assert(std::is_base_of<Component, T>::value, "Component only");
            if (IsUniqueComponent<T>() && GetComponent<T>() != nullptr)
            {
                return nullptr;
            }
            auto component = std::make_shared<T>();
            component->m_gameObject = m_transform->gameObject();
            m_components.push_back(component);
            return component;
        }

        template<class T, std::enable_if_t<std::is_base_of<Script, T>::value, int> = 42>
        std::shared_ptr<T>
        AddComponent()
        {
            static_assert(std::is_base_of<Script, T>::value, "Script only");
            auto script = std::make_shared<T>();
            script->m_gameObject = m_transform->gameObject();
            m_scripts.push_back(script);
            return script;
        }


        void RemoveComponent(PComponent component)
        {
            m_components.remove(component);
        }

        void RemoveScript(PScript script)
        {
            m_scripts.remove(script);
        }

        // Activates/Deactivates the GameObject (activeSelf).
        void SetActive(bool value) {
            m_activeSelf = value;
        }

        /************************************************************************/
        /*                    Static Functions                                  */
        /************************************************************************/

        // Finds a game object by name and returns it.
        static PGameObject Find(const std::string& name);


    protected:
        friend class Scene;
        void Start();
        void Update();
        void OnDrawGizmos();

    private:
        friend class FishEditor::EditorGUI;
        std::list<PComponent> m_components;
        std::list<PScript> m_scripts;

        bool m_activeSelf = true;
        int m_layer = 0;

        std::string m_tag;
        PTransform m_transform;

        //static GameObject m_root;
        //static PGameObject> m_root;
    };
}

#endif // GameObject_hpp
