// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2025 Kacper Donat <kacper@kadet.net>                     *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include "ParameterManager.h"
#include "DynamicStyleParameterProvider.h"
#include "Parser.h"

#include <QFile>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <fmt/ranges.h>

#include <QApplication>
#include <QEvent>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QVariant>
#include <QWidget>
#include <ranges>
#include <utility>
#include <variant>

#include <Base/Console.h>

FC_LOG_LEVEL_INIT("Gui", true, true)

namespace Gui::StyleParameters
{

/**
 * App-level event filter that intercepts QEvent::Polish on every widget and
 * drives override computation in ParameterManager. Installing on QApplication
 * means ParameterManager owns the full lifecycle — FreeCADStyle only needs to
 * call clearOverrideCache on unpolish.
 */
class OverrideComputeEventListener: public QObject
{
    Q_OBJECT

public:
    explicit OverrideComputeEventListener(ParameterManager* manager)
        : QObject(nullptr)
        , _manager(manager)
    {
        QApplication::instance()->installEventFilter(this);
    }

    ~OverrideComputeEventListener() override
    {
        if (QApplication::instance()) {
            QApplication::instance()->removeEventFilter(this);
        }
    }

    FC_DEFAULT_COPY_MOVE(OverrideComputeEventListener);

    bool eventFilter(QObject* obj, QEvent* event) override
    {
        if (event->type() == QEvent::Polish) {
            if (auto* widget = qobject_cast<QWidget*>(obj)) {
                _manager->ensureOverridesAreComputed(widget);

                disconnect(
                    widget,
                    &QObject::destroyed,
                    this,
                    &OverrideComputeEventListener::handleWidgetDestroy
                );
                connect(
                    widget,
                    &QObject::destroyed,
                    this,
                    &OverrideComputeEventListener::handleWidgetDestroy
                );
            }
        }

        return false;
    }

public Q_SLOTS:
    void handleWidgetDestroy(QObject* widget)
    {
        _manager->clearOverrideCache(static_cast<QWidget*>(widget));
    }

private:
    ParameterManager* _manager;
};

namespace
{

/// Opens a YAML file at the given path (supports "qss:" Qt search-path scheme) and
/// returns the parsed root node. Returns an empty node and logs a warning on failure.
YAML::Node loadYamlFile(const std::string& path)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        FC_WARN("StyleParameters: Unable to open file " << path);
        return YAML::Node {};
    }
    QTextStream stream(&file);
    return YAML::Load(stream.readAll().toStdString());
}

/// Resolves a relative path against a base file path by replacing the last path
/// component of 'base' with 'relative'. Preserves any scheme prefix (e.g. "qss:").
std::string resolveRelativePath(const std::string& base, const std::string& relative)
{
    auto slashPos = base.rfind('/');
    if (slashPos == std::string::npos) {
        return relative;
    }
    return base.substr(0, slashPos + 1) + relative;
}

/// Converts a YAML node to a StyleParameters expression string.
/// Scalars are returned as-is; sequences become unnamed tuples "(a, b, ...)";
/// maps become named tuples "(key1: val1, key2: val2, ...)". Recursive.
std::string yamlNodeToExpression(const YAML::Node& node)
{
    if (node.IsScalar()) {
        return node.as<std::string>();
    }

    if (node.IsSequence()) {
        std::vector<std::string> parts;
        parts.reserve(node.size());
        for (const auto& element : node) {
            parts.push_back(yamlNodeToExpression(element));
        }
        return fmt::format("({})", fmt::join(parts, ", "));
    }

    if (node.IsMap()) {
        std::vector<std::string> parts;
        parts.reserve(node.size());
        for (auto it = node.begin(); it != node.end(); ++it) {
            parts.push_back(
                fmt::format("{}: {}", it->first.as<std::string>(), yamlNodeToExpression(it->second))
            );
        }
        return fmt::format("({})", fmt::join(parts, ", "));
    }

    return "";
}

/// Formats a gradient tuple as QSS qlineargradient() or qradialgradient().
std::string gradientToQss(const Tuple& tuple, const auto& formatValue)
{
    const char* functionName = tuple.kind == TupleKind::LinearGradient ? "qlineargradient"
                                                                       : "qradialgradient";

    std::vector<std::string> parts;

    // Geometry params (all named elements except "stops")
    for (const auto& [name, value] : tuple.elements) {
        if (!name || *name == "stops") {
            continue;
        }
        parts.push_back(fmt::format("{}:{}", *name, formatValue(*value)));
    }

    // Stops
    const auto* stopsValue = tuple.find("stops");
    if (stopsValue && stopsValue->holds<Tuple>()) {
        const auto& stopsTuple = stopsValue->get<Tuple>();
        for (size_t index = 0; index < stopsTuple.size(); ++index) {
            const auto& stopEntry = stopsTuple.at(index).get<Tuple>();
            parts.push_back(
                fmt::format("stop:{} {}", formatValue(stopEntry.at(0)), formatValue(stopEntry.at(1)))
            );
        }
    }

    return fmt::format("{}({})", functionName, fmt::join(parts, ", "));
}

/// Formats a Value for QSS output.
/// Gradient tuples use qlineargradient()/qradialgradient() syntax.
/// Other tuples become space-separated values (e.g. "10px 5px 10px 5px").
/// All other types delegate to toString().
std::string toQss(const Value& value)
{
    if (value.holds<None>()) {
        return "";
    }

    if (value.holds<Tuple>()) {
        const auto& tuple = value.get<Tuple>();

        if (tuple.kind == TupleKind::LinearGradient || tuple.kind == TupleKind::RadialGradient) {
            return gradientToQss(tuple, [](const Value& val) { return toQss(val); });
        }

        std::vector<std::string> parts;
        parts.reserve(tuple.elements.size());

        for (const auto& [name, elem] : tuple.elements) {
            parts.push_back(toQss(*elem));
        }

        return fmt::format("{}", fmt::join(parts, " "));
    }

    return value.toString();
}

}  // namespace

ParameterSource::ParameterSource(const Metadata& metadata)
    : metadata(metadata)
{}

InMemoryParameterSource::InMemoryParameterSource(
    const std::list<Parameter>& parameters,
    const Metadata& metadata
)
    : ParameterSource(metadata)
{
    for (const auto& parameter : parameters) {
        InMemoryParameterSource::define(parameter);
    }
}

std::list<Parameter> InMemoryParameterSource::all() const
{
    auto values = parameters | std::ranges::views::values;

    return std::list<Parameter>(values.begin(), values.end());
}

std::optional<Parameter> InMemoryParameterSource::get(const std::string& name) const
{
    if (parameters.contains(name)) {
        return parameters.at(name);
    }

    return std::nullopt;
}

void InMemoryParameterSource::define(const Parameter& parameter)
{
    parameters[parameter.name] = parameter;
}

void InMemoryParameterSource::remove(const std::string& name)
{
    parameters.erase(name);
}

BuiltInParameterSource::BuiltInParameterSource(const Metadata& metadata)
    : ParameterSource(metadata)
{
    this->metadata.options |= ReadOnly;
}

std::list<Parameter> BuiltInParameterSource::all() const
{
    std::list<Parameter> result;

    for (const auto& name : params | std::views::keys) {
        result.push_back(*get(name));
    }

    return result;
}

std::optional<Parameter> BuiltInParameterSource::get(const std::string& name) const
{
    if (params.contains(name)) {
        unsigned long color = params.at(name)->GetUnsigned(name.c_str(), 0);

        return Parameter {
            .name = name,
            .value = fmt::format("#{:0>6x}", 0x00FFFFFF & (color >> 8)),  // NOLINT(*-magic-numbers)
        };
    }

    return std::nullopt;
}

UserParameterSource::UserParameterSource(ParameterGrp::handle hGrp, const Metadata& metadata)
    : ParameterSource(metadata)
    , hGrp(hGrp)
{}

std::list<Parameter> UserParameterSource::all() const
{
    std::list<Parameter> result;

    for (const auto& [token, value] : hGrp->GetASCIIMap()) {
        result.push_back({
            .name = token,
            .value = value,
        });
    }

    return result;
}

std::optional<Parameter> UserParameterSource::get(const std::string& name) const
{
    if (auto value = hGrp->GetASCII(name.c_str(), ""); !value.empty()) {
        return Parameter {
            .name = name,
            .value = value,
        };
    }

    return {};
}

void UserParameterSource::define(const Parameter& parameter)
{
    hGrp->SetASCII(parameter.name.c_str(), parameter.value);
}

void UserParameterSource::remove(const std::string& name)
{
    hGrp->RemoveASCII(name.c_str());
}

YamlParameterSource::YamlParameterSource(const std::string& filePath, const Metadata& metadata)
    : ParameterSource(metadata)
{
    changeFilePath(filePath);
}

void YamlParameterSource::changeFilePath(const std::string& path)
{
    this->filePath = path;
    reload();
}

void YamlParameterSource::reload()
{
    if (filePath.starts_with(":/")) {
        this->metadata.options |= ReadOnly;
    }

    const YAML::Node root = loadYamlFile(filePath);

    inheritPaths.clear();
    if (root["_inherits"]) {
        const auto& inheritsNode = root["_inherits"];
        if (inheritsNode.IsScalar()) {
            inheritPaths.push_back(inheritsNode.as<std::string>());
        }
        else if (inheritsNode.IsSequence()) {
            for (const auto& entry : inheritsNode) {
                inheritPaths.push_back(entry.as<std::string>());
            }
        }
    }

    std::map<std::string, ParameterEntry> ownEntries;
    for (auto it = root.begin(); it != root.end(); ++it) {
        const auto key = it->first.as<std::string>();
        if (key.starts_with("_")) {
            continue;
        }
        const auto value = yamlNodeToExpression(it->second);
        ownEntries[key] = ParameterEntry {
            .parameter = {.name = key, .value = value},
            .inherited = false,
        };
    }

    rebuildMergedView(ownEntries);
}

void YamlParameterSource::rebuildMergedView(const std::map<std::string, ParameterEntry>& ownEntries)
{
    parameters.clear();

    for (const auto& relativePath : inheritPaths) {
        const YAML::Node inheritedRoot = loadYamlFile(resolveRelativePath(filePath, relativePath));
        for (auto it = inheritedRoot.begin(); it != inheritedRoot.end(); ++it) {
            const auto key = it->first.as<std::string>();
            if (key.starts_with("_")) {
                continue;  // skip meta-keys; recursive _inherits not supported
            }
            const auto value = yamlNodeToExpression(it->second);
            parameters[key] = ParameterEntry {
                .parameter = {.name = key, .value = value},
                .inherited = true,
            };
        }
    }

    for (const auto& [name, entry] : ownEntries) {
        parameters[name] = entry;
    }
}

void YamlParameterSource::rebuildMergedView()
{
    std::map<std::string, ParameterEntry> ownEntries;
    for (const auto& [name, entry] : parameters) {
        if (!entry.inherited) {
            ownEntries[name] = entry;
        }
    }
    rebuildMergedView(ownEntries);
}

std::list<Parameter> YamlParameterSource::all() const
{
    std::list<Parameter> result;
    for (const auto& [name, entry] : parameters) {
        result.push_back(entry.parameter);
    }
    return result;
}

std::optional<Parameter> YamlParameterSource::get(const std::string& name) const
{
    if (auto it = parameters.find(name); it != parameters.end()) {
        return it->second.parameter;
    }

    return std::nullopt;
}

void YamlParameterSource::define(const Parameter& param)
{
    parameters[param.name] = ParameterEntry {.parameter = param, .inherited = false};
}

void YamlParameterSource::remove(const std::string& name)
{
    parameters.erase(name);
    rebuildMergedView();  // restore inherited value for 'name' if present in a parent file
}

void YamlParameterSource::flush()
{
    YAML::Node root;

    if (!inheritPaths.empty()) {
        if (inheritPaths.size() == 1) {
            root["_inherits"] = inheritPaths.front();
        }
        else {
            YAML::Node sequence(YAML::NodeType::Sequence);
            for (const auto& path : inheritPaths) {
                sequence.push_back(path);
            }
            root["_inherits"] = sequence;
        }
    }

    for (const auto& [name, entry] : parameters) {
        if (!entry.inherited) {
            root[name] = entry.parameter.value;
        }
    }

    QFile file(QString::fromStdString(filePath));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        FC_WARN("StyleParameters: Unable to open file " << filePath);
        return;
    }

    QTextStream out(&file);
    out << QString::fromStdString(YAML::Dump(root));
}

ParameterManager::ParameterManager() = default;
ParameterManager::~ParameterManager() = default;
ParameterManager::ParameterManager(ParameterManager&&) noexcept = default;

void ParameterManager::setResolver(StyleParameterResolver* resolver)
{
    _resolver = resolver;
}

ParameterDescriptorRegistry& ParameterManager::descriptorRegistry()
{
    return _descriptorRegistry;
}

const ParameterDescriptorRegistry& ParameterManager::descriptorRegistry() const
{
    return _descriptorRegistry;
}

void ParameterManager::reload()
{
    _resolved.clear();
    _widgetResolved.clear();
    _widgetOverrides.clear();
    if (_resolver) {
        _resolver->refresh();
    }
}

std::string ParameterManager::replacePlaceholders(
    const std::string& expression,
    ResolveContext context
) const
{
    // Matches @TokenName (group name) or @{expression} (group expression)
    static const QRegularExpression regex("@(?:(?P<name>\\w+)|({(?P<expression>(?>[^{}]+|(?2))+)}))");

    auto substituteWithCallback =
        [](const QRegularExpression& regex,
           const QString& input,
           const std::function<QString(const QRegularExpressionMatch&)>& callback) {
            QRegularExpressionMatchIterator it = regex.globalMatch(input);

            QString result;
            qsizetype lastIndex = 0;

            while (it.hasNext()) {
                QRegularExpressionMatch match = it.next();

                qsizetype start = match.capturedStart();
                qsizetype end = match.capturedEnd();

                result += input.mid(lastIndex, start - lastIndex);
                result += callback(match);

                lastIndex = end;
            }

            // Append any remaining text after the last match
            result += input.mid(lastIndex);

            return result;
        };

    // clang-format off
    return substituteWithCallback(
        regex,
        QString::fromStdString(expression),
        [&](const QRegularExpressionMatch& match) -> QString {
            // Group 1: @TokenName
            if (!match.captured("name").isEmpty()) {
                auto tokenName = match.captured(1).toStdString();
                auto tokenValue = resolve(tokenName, context);

                if (!tokenValue) {
                    Base::Console().warning("Requested non-existent style parameter token '%s'.\n", tokenName);
                    return QStringLiteral("");
                }

                context.visited.erase(tokenName);
                return QString::fromStdString(toQss(*tokenValue));
            }

            // Group 2: @{expression}
            auto exprBody = match.captured("expression").toStdString();
            try {
                Value result = evaluate(exprBody, context);
                return QString::fromStdString(toQss(result));
            }
            catch (Base::Exception& e) {
                Base::Console().warning(
                    "Failed to evaluate inline expression '@{%s}': %s\n",
                    exprBody,
                    e.what()
                );
                return QStringLiteral("");
            }
    }
    ).toStdString();
    // clang-format on
}

std::list<Parameter> ParameterManager::parameters() const
{
    std::set<Parameter, Parameter::NameComparator> result;

    // we need to traverse it in reverse order so more important tokens will take precedence
    for (const ParameterSource* source : _sources | std::views::reverse) {
        for (const Parameter& parameter : source->all()) {
            result.insert(parameter);
        }
    }

    return std::list(result.begin(), result.end());
}

std::optional<std::string> ParameterManager::expression(const std::string& name) const
{
    if (auto param = parameter(name)) {
        return param->value;
    }

    return {};
}

std::optional<Value> ParameterManager::resolve(const std::string& name) const
{
    if (_resolver) {
        return _resolver->resolve(name, this);
    }
    return resolve(name, ResolveContext {});
}

std::optional<Value> ParameterManager::resolve(const std::string& name, ResolveContext context) const
{
    if (context.visited.contains(name)) {
        Base::Console().warning("The style parameter '%s' contains circular-reference.\n", name);
        return expression(name);
    }

    // Widget-aware path: the widget's override set (computed at polish time)
    // drives both resolution and caching. Once a widget owns any override
    // every resolution in its context lands in the per-widget cache so
    // widget-scoped values never leak into the global _resolved map.
    if (context.widget && hasOverrides(context.widget)) {
        const auto it = _widgetOverrides.find(context.widget);
        if (it != _widgetOverrides.end() && !it->second.empty()) {
            return resolveForWidget(name, std::move(context), it->second);
        }
    }

    return resolveFlat(name, std::move(context));
}

std::optional<Value> ParameterManager::resolveFlat(const std::string& name, ResolveContext context) const
{
    std::optional<Parameter> maybeParameter = this->parameter(name);

    if (!maybeParameter) {
        return std::nullopt;
    }

    const Parameter& token = *maybeParameter;

    if (!_resolved.contains(token.name)) {
        context.visited.insert(token.name);
        try {
            _resolved[token.name] = evaluate(token.value, context);
        }
        catch (Base::Exception&) {
            // in case of being unable to parse it, we need to treat it as a generic value
            _resolved[token.name] = replacePlaceholders(token.value, context);
        }
        context.visited.erase(token.name);
    }

    return _resolved[token.name];
}

std::optional<Value> ParameterManager::resolveForWidget(
    const std::string& name,
    ResolveContext context,
    const std::unordered_map<std::string, Value>& overrides
) const
{
    auto& widgetCache = _widgetResolved[context.widget];
    if (const auto it = widgetCache.find(name); it != widgetCache.end()) {
        return it->second;
    }

    // Direct override wins — no source lookup, no expression evaluation.
    if (const auto it = overrides.find(name); it != overrides.end()) {
        widgetCache[name] = it->second;
        return it->second;
    }

    // Normal source lookup + expression evaluation, but cached per widget.
    // Nested @refs recurse through the same widget-bearing context (Parser
    // forwards ResolveContext through evaluation), so they too land in the
    // widget cache and never pollute _resolved.
    std::optional<Parameter> maybeParameter = this->parameter(name);
    if (!maybeParameter) {
        widgetCache[name] = std::nullopt;
        return std::nullopt;
    }

    const Parameter& token = *maybeParameter;
    context.visited.insert(token.name);
    std::optional<Value> result;
    try {
        result = evaluate(token.value, context);
    }
    catch (Base::Exception&) {
        result = replacePlaceholders(token.value, context);
    }
    context.visited.erase(token.name);

    widgetCache[name] = result;
    return result;
}

Value ParameterManager::evaluate(const std::string& expression, ResolveContext context) const
{
    Parser parser(expression);
    return parser.parse()->evaluate({.manager = this, .context = std::move(context)});
}

std::optional<Parameter> ParameterManager::parameter(const std::string& name) const
{
    for (const ParameterSource* source : _sources) {
        if (const auto& parameter = source->get(name)) {
            return parameter;
        }
    }

    return {};
}

void ParameterManager::addSource(ParameterSource* source)
{
    _sources.push_front(source);
}

std::list<ParameterSource*> ParameterManager::sources() const
{
    return _sources;
}

namespace
{
/// Marker dynamic property set by FreeCADStyle::polish after buildOverrides
/// finds at least one override for the widget. Read by hasApplicableProviders
/// as a constant-time gate on the resolve hot path — no provider iteration.
constexpr const char* overridesMarkerProperty = "hasStyleOverrides";
}  // namespace

void ParameterManager::addDynamicProvider(std::shared_ptr<DynamicStyleParameterProvider> provider)
{
    if (!provider) {
        return;
    }
    const int priority = provider->priority();
    const auto insertionPoint
        = std::ranges::upper_bound(_dynamicProviders, priority, {}, [](const auto& existing) {
              return existing->priority();
          });
    _dynamicProviders.insert(insertionPoint, std::move(provider));

    if (!_polishObserver) {
        _polishObserver = std::make_unique<OverrideComputeEventListener>(this);
    }

    // Per-widget state was built from a stale provider list; drop it so the
    // next polish sweep rebuilds correctly. Global _resolved is unaffected
    // because widget-scoped values never land there.
    _widgetResolved.clear();
    _widgetOverrides.clear();
}

void ParameterManager::removeDynamicProvider(const DynamicStyleParameterProvider* provider)
{
    const auto erased = std::erase_if(_dynamicProviders, [provider](const auto& stored) {
        return stored.get() == provider;
    });

    if (erased > 0) {
        _widgetResolved.clear();
        _widgetOverrides.clear();
    }
}

void ParameterManager::ensureOverridesAreComputed(const QWidget* widget) const
{
    if (!widget) {
        return;
    }

    if (_widgetOverrides.contains(widget)) {
        return;
    }

    // Merge in ascending priority: first writer wins so lower-priority
    // providers (explicit user overrides) beat higher-priority computed
    // fallbacks on name collisions.
    std::unordered_map<std::string, Value> merged;
    for (const auto& provider : _dynamicProviders) {
        for (auto&& [name, value] : provider->overridesFor(widget)) {
            merged.try_emplace(name, std::move(value));
        }
    }

    // The const_cast is considered bad practice in general, but here we consider the marker
    // as essentially `mutable` property as it is used to improve performance.
    const_cast<QWidget*>(widget)->setProperty(overridesMarkerProperty, !merged.empty());

    if (merged.empty()) {
        _widgetOverrides.erase(widget);
        _widgetResolved.erase(widget);
        return;
    }

    _widgetOverrides[widget] = std::move(merged);
    _widgetResolved.erase(widget);  // previously cached evals may be stale
}

bool ParameterManager::hasOverrides(const QWidget* widget) const
{
    if (!widget) {
        return false;
    }
    return widget->property(overridesMarkerProperty).toBool();
}

StyleParameterOverrides ParameterManager::getOverrides(const QWidget* widget) const
{
    ensureOverridesAreComputed(widget);

    if (!hasOverrides(widget)) {
        return {};
    }

    return _widgetOverrides.at(widget);
}

void ParameterManager::clearOverrideCache(const QWidget* widget)
{
    // The const_cast is considered bad practice in general, but here we are removing the marker
    // used for improving performance that we essentially consider as part of caching strategy and
    // hence safe to mutate.
    const_cast<QWidget*>(widget)->setProperty(overridesMarkerProperty, QVariant {});

    _widgetResolved.erase(widget);
    _widgetOverrides.erase(widget);
}

}  // namespace Gui::StyleParameters

#include "ParameterManager.moc"
