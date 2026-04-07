# Active Todo Notes

Scene:
* a scene consists of entities
* an entity consists of components
* a component consists of data
* a component can be attached to an entity

Systems:
* a system can be active or inactive
* a system can be added to a scene
* a system can react to events by registering to the entt::dispatcher of the scene
* an active system will operate on entities with the required components and will react to events
* an inactive system will not operate on entities with the required components and will not react to events
* a system has only plain functions

Gui:
* main source of events is the gui
* the gui renders active widgets and the user can configure parameter structs which are then passed as events to the systems

