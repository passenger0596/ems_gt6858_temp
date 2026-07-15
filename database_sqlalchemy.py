# 1. 导入必要模块
from collections import defaultdict
from sqlalchemy import create_engine, Column, Integer, String, Float, Boolean, DateTime,text
from sqlalchemy.orm import declarative_base, sessionmaker
from sqlalchemy.inspection import inspect
from logger import running_logger
import datetime
import gc
import json
import os
import config

# 假设 bms.py 中的 GtBms 类已定义，如果未导入，请添加：
# from bms import GtBms  # 根据实际路径导入

# 2. 创建数据库引擎（使用config中配置的路径）
engine = create_engine(config.MAIN_DATABASE_FILEPATH, echo=False)
running_logger.info(f"数据库引擎初始化，使用路径: {config.MAIN_DATABASE_FILEPATH}")
_DB_BASE = os.path.dirname(os.path.abspath(__file__))
engine_log = create_engine(
    f'sqlite:///{os.path.join(_DB_BASE, "operation_log.db")}',
    connect_args={'timeout': 30},  # 增加超时时间
    echo=False
)


# 历史告警
almHis_engine = create_engine('sqlite:///alarmHistory.db',echo=False)

# 3. 创建基类
Base = declarative_base()
Base_log=declarative_base()
Base_almHis=declarative_base()

# 在文件顶部添加缓存
_checked_tables = set()
_cached_log_tables = set()

class OperationLog(Base_log):
    __tablename__ = 'operation'
    id = Column(Integer, primary_key=True, autoincrement=True)
    timestamp = Column(DateTime, default=datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'))
    type = Column(String(20), nullable=False)
    source = Column(String(20), nullable=False)
    desc = Column(String(50), nullable=False)

class AlarmHistory(Base_almHis):
    __tablename__ = 'alarmHistory'
    id = Column(Integer, primary_key=True, autoincrement=True)
    等级 = Column(String, name='等级')
    告警时间 = Column(String, name='告警时间', default=datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'))
    设备名称 = Column(String, name='设备名称')
    告警描述 = Column(String, name='告警描述')
    恢复时间 = Column(String, name='恢复时间', default=datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'))

# 创建表
Base_log.metadata.create_all(engine_log)
Base_almHis.metadata.create_all(almHis_engine)

def insert_almHis(level, startTime, device, desc,recoveryTime='NA')->bool:
    Session = sessionmaker(bind=almHis_engine)
    session = Session()
    try:
        # 创建新的告警记录
        new_alarm = AlarmHistory(
            等级=level,
            设备名称=device,
            告警描述=desc,
            告警时间=startTime,
            恢复时间='NA' if recoveryTime is None else recoveryTime
        )
        # 添加到会话并提交
        session.add(new_alarm)
        session.commit()
        running_logger.info(f"告警记录{level}:{device}:{desc}插入成功!")
        return True
    except Exception as e:
        running_logger.error(f"插入告警记录时出错: {str(e)}")
        return False
    finally:
        if 'session' in locals():
            session.close()

def update_recoveryTime(startTime,device,desc,recoveryTime=None)->bool:
    Session = sessionmaker(bind=almHis_engine)
    session = Session()
    if recoveryTime is None:
        recoveryTime = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    try:
        alarm_record = session.query(AlarmHistory).filter(AlarmHistory.告警时间 == startTime if startTime is not None else text("1=1"),
                                                        AlarmHistory.设备名称==device,
                                                        AlarmHistory.告警描述==desc).first()
        if alarm_record:
            # 更新恢复时间字段
            alarm_record.恢复时间 = recoveryTime
            session.commit()
            running_logger.info(f"成功更新设备 '{device}:{desc}' 的告警恢复时间")
            return True
        else:
            running_logger.warning(f"未找到匹配的告警记录: 告警时间={startTime}, 设备名称={device}, 告警描述={desc}")
            return False
    except Exception as e:
        running_logger.error(f"更新恢复时间时出错: {str(e)}")
        session.rollback()
        return False
    finally:
        session.close()


def add_log(timestamp, type, source, desc):
    Session =sessionmaker(bind=engine_log)
    session = Session()  # 创建会话实例
    try:
        # 如果 timestamp 是字符串，转换为 datetime 并截断到秒
        if isinstance(timestamp, str):
            timestamp = datetime.datetime.strptime(timestamp, '%Y-%m-%d %H:%M:%S.%f')
        # 截断到秒（去掉毫秒）
        timestamp = timestamp.replace(microsecond=0)
        operation_log = OperationLog(timestamp=timestamp, type=type, source=source, desc=desc)
        session.add(operation_log)
        session.commit()
        running_logger.info(f"日志插入成功: {desc}")
    except Exception as e:
        session.rollback()
        running_logger.error(f"日志插入失败: {e}")
    finally:
        session.close()





# 4. 字段类型映射
TYPE_MAPPING = {
    "String": String,
    "Integer": Integer,
    "Float": Float,
    "FLOAT": Float,
    "Boolean": Boolean,
    "Bool": Boolean,
    "bool":Boolean,
    "BOOL": Boolean,
    "DateTime": DateTime,
    "INT":Integer,
    "INT16": Integer,
    "UINT16": Integer,
    "INT32": Integer,
    "UINT32": Integer,
    "UINT": Integer,
    "DOUBLE": Float,
}

global_models = {}  # 键为 table_name（如 dcdc1）

# 5. 动态创建模型的函数 (修改为支持传入 data_dict、自定义 table_name 和 class_name)
# def create_model_class(device=None, data_dict=None, custom_table_name=None, custom_class_name=None):
#     """根据设备类动态创建 SQLAlchemy 模型"""
#     if device:
#         # 处理类名
#         class_name = device.name.capitalize()  # 类名：Dcdc1, Pcs1
#         table_name = device.name.lower()  # 表名：dcdc1, pcs1
#         used_data_dict = device.data_dict
#     else:
#         class_name = custom_class_name
#         table_name = custom_table_name
#         used_data_dict = data_dict
#
#     # 检查是否已存在模型
#     if table_name in global_models:
#         running_logger.info(f"复用模型类 {class_name}，表名 {table_name}")
#         return global_models[table_name]
#
#     # 检查表是否已存在
#     if table_name in Base.metadata.tables:
#         running_logger.info(f"表 {table_name} 已存在，复用模型")
#         return Base.metadata.tables[table_name].cls
#
#     # 动态生成模型类的属性
#     attrs = {
#         "__tablename__": table_name,
#         "id": Column(Integer, primary_key=True),  # 每个表添加主键
#         "timestamp": Column(DateTime, default=datetime.datetime.utcnow),
#         "online_status": Column(Boolean, server_default='0', nullable=False),
#         "__table_args__": {"extend_existing": True}
#     }
#
#     # 添加字段，定义每列字段的各个类型、属性
#     try:
#         for field_name, field_info in used_data_dict.items():
#             field_type = TYPE_MAPPING[field_info["datatype"]]
#             field_args = []
#             if field_info["datatype"] in ["String", "STRING"]:
#                 field_args.append(field_info.get("length", 10))
#             attrs[field_name] = Column(
#                 field_type(*field_args),
#                 unique=field_info.get("unique", False),
#                 nullable=field_info.get("nullable", True)
#             )
#     except KeyError as e:
#         running_logger.error(f"无效的字段类型: {e}")
#         raise
#
#     # 动态创建模型类
#     model_class = type(class_name, (Base,), attrs)
#     global_models[table_name] = model_class
#     running_logger.info(f"创建模型类 {class_name}，表名 {table_name}")
#     return model_class

def escape_sql_identifier(identifier):
    """转义SQL标识符（表名、字段名）"""
    # 使用反引号转义，这是SQL标准做法
    return f'`{identifier.replace("`", "``")}`'

# 5. 动态创建模型的函数 (修改为支持传入 data_dict、自定义 table_name 和 class_name)
def create_model_class(device=None, data_dict=None, custom_table_name=None, custom_class_name=None):
    """根据设备类动态创建 SQLAlchemy 模型，支持自动添加新字段"""
    if device:
        # 处理类名
        class_name = device.name.capitalize()  # 类名：Dcdc1, Pcs1
        table_name = device.name.lower()  # 表名：dcdc1, pcs1
        used_data_dict = device.data_dict
    else:
        class_name = custom_class_name
        table_name = custom_table_name
        used_data_dict = data_dict

    # 检查表是否已存在
    inspector = inspect(engine)
    table_exists = inspector.has_table(table_name)

    # 检查是否已存在模型，但即使存在也要检查表结构变化
    model_class = None
    if table_name in global_models:
        running_logger.info(f"复用模型类 {class_name}，表名 {table_name}")
        model_class = global_models[table_name]

        # 如果表存在，检查是否需要添加新字段
        if table_exists:
            # 获取现有表的列信息
            existing_columns = {col['name'] for col in inspector.get_columns(table_name)}

            # 检测需要添加的新字段（过滤已存在的字段）
            new_fields = []
            for field_name, field_info in used_data_dict.items():
                if field_name not in existing_columns:
                    new_fields.append(field_name)
                else:
                    running_logger.debug(f"字段 {field_name} 已存在，跳过添加")

            # 添加新字段到现有表
            if new_fields:
                Session = sessionmaker(bind=engine)
                session = Session()

                try:
                    for field_name in new_fields:
                        field_info = used_data_dict[field_name]

                        # 构建SQL类型
                        if field_info["datatype"] in ["String", "STRING"]:
                            length = field_info.get("length", 10)
                            sql_type = f"VARCHAR({length})"
                        elif field_info["datatype"] in ["Integer", "INT", "INT16", "UINT16", "INT32", "UINT32", "UINT"]:
                            sql_type = "INTEGER"
                        elif field_info["datatype"] in ["Float", "FLOAT", "DOUBLE"]:
                            sql_type = "FLOAT"
                        elif field_info["datatype"] in ["Boolean", "Bool", "bool", "BOOL"]:
                            sql_type = "BOOLEAN"
                        else:
                            sql_type = "TEXT"

                        # 转义标识符
                        escaped_table_name = escape_sql_identifier(table_name)
                        escaped_field_name = escape_sql_identifier(field_name)

                        alter_sql = f'ALTER TABLE {escaped_table_name} ADD COLUMN {escaped_field_name} {sql_type}'

                        try:
                            # 再次检查字段是否存在（防止并发问题）
                            current_columns = {col['name'] for col in inspector.get_columns(table_name)}
                            if field_name in current_columns:
                                running_logger.warning(f"字段 {field_name} 已存在，跳过添加")
                                continue

                            session.execute(text(alter_sql))
                            running_logger.info(f"为表 {table_name} 添加新字段: {field_name}")
                        except Exception as e:
                            if "duplicate column name" in str(e).lower():
                                running_logger.warning(f"字段 {field_name} 已存在，跳过添加")
                            else:
                                running_logger.error(f"添加字段 {field_name} 失败: {e}")
                                # 继续处理其他字段，不中断整个流程

                    session.commit()
                    running_logger.info(f"表 {table_name} 结构更新完成，新增 {len(new_fields)} 个字段")

                except Exception as e:
                    session.rollback()
                    running_logger.error(f"更新表结构失败: {e}")
                finally:
                    session.close()

    # 如果模型类不存在或需要重新创建，则创建新的模型类
    if model_class is None:
        # 动态生成模型类的属性
        attrs = {
            "__tablename__": table_name,
            "id": Column(Integer, primary_key=True),  # 每个表添加主键
            "timestamp": Column(DateTime, default=datetime.datetime.utcnow),
            "online_status": Column(Boolean, server_default='0', nullable=False),
            "__table_args__": {"extend_existing": True}
        }

        # 添加字段，定义每列字段的各个类型、属性
        try:
            for field_name, field_info in used_data_dict.items():
                field_type = TYPE_MAPPING[field_info["datatype"]]
                field_args = []
                if field_info["datatype"] in ["String", "STRING"]:
                    field_args.append(field_info.get("length", 10))
                attrs[field_name] = Column(
                    field_type(*field_args),
                    unique=field_info.get("unique", False),
                    nullable=field_info.get("nullable", True)
                )
        except KeyError as e:
            running_logger.error(f"无效的字段类型: {e}")
            raise

        # 动态创建模型类
        model_class = type(class_name, (Base,), attrs)
        global_models[table_name] = model_class
        running_logger.info(f"创建模型类 {class_name}，表名 {table_name}")

    return model_class

# 6. 动态导入设备类并创建表，返回设备的模型类
def create_tables(device_list):
    """从设备列表导入类并创建表"""
    try:
        for device in device_list:
            table_name = device.name.lower()
            if table_name not in global_models:
                global_models[table_name] = create_model_class(device)

            # 针对 BMS (GtBms) 的特殊处理，创建 cluster 表
            if device.name.lower() == "gt_bms_tcp":  # 或使用 isinstance(device, GtBms) 如果已导入 GtBms
                for i, cluster_data in enumerate(device.list_for_data_cluster_dict, start=1):
                    cluster_table_name = f"cluster{i}"
                    cluster_class_name = f"Cluster{i}"
                    if cluster_table_name not in global_models:
                        global_models[cluster_table_name] = create_model_class(
                            data_dict=cluster_data,
                            custom_table_name=cluster_table_name,
                            custom_class_name=cluster_class_name
                        )

        # 创建表
        Base.metadata.create_all(engine)
        running_logger.info('所有表创建成功')
    except Exception as e:
        running_logger.error(f"创建表失败: {e}")
        return []
    return list(global_models.values())


def check_and_table_structure_once(engine, table_name, expected_fields):
    """一次性检查并更新表结构，避免重复检查"""
    inspector = inspect(engine)

    # 检查表是否存在
    if not inspector.has_table(table_name):
        running_logger.error(f"表 {table_name} 不存在")
        return False

    # 获取现有字段（使用精确匹配）
    existing_columns = {col['name'] for col in inspector.get_columns(table_name)}
    expected_columns = set(expected_fields.keys())

    # 使用精确匹配找出缺失的字段
    missing_columns = expected_columns - existing_columns

    if not missing_columns:
        running_logger.debug(f"表 {table_name} 结构完整，无需更新")  # 改为debug级别，减少日志
        return True

    running_logger.info(f"表 {table_name} 缺失字段: {missing_columns}")

    # 添加缺失的字段
    Session = sessionmaker(bind=engine)
    session = Session()

    success_count = 0
    try:
        for field_name in missing_columns:
            # 再次检查字段是否存在（避免并发问题）
            current_columns = {col['name'] for col in inspector.get_columns(table_name)}
            if field_name in current_columns:
                running_logger.debug(f"字段 {field_name} 已存在，跳过添加")
                continue

            field_info = expected_fields[field_name]

            # 构建SQL类型
            if field_info["datatype"] in ["String", "STRING"]:
                length = field_info.get("length", 10)
                sql_type = f"VARCHAR({length})"
            elif field_info["datatype"] in ["Integer", "INT", "INT16", "UINT16", "INT32", "UINT32", "UINT"]:
                sql_type = "INTEGER"
            elif field_info["datatype"] in ["Float", "FLOAT", "DOUBLE"]:
                sql_type = "FLOAT"
            elif field_info["datatype"] in ["Boolean", "Bool", "bool", "BOOL"]:
                sql_type = "BOOLEAN"
            else:
                sql_type = "TEXT"

            # 转义字段名
            escaped_field_name = f'"{field_name}"'

            alter_sql = f'ALTER TABLE "{table_name}" ADD COLUMN {escaped_field_name} {sql_type}'

            try:
                session.execute(text(alter_sql))
                running_logger.info(f"成功为表 {table_name} 添加字段: {field_name}")
                success_count += 1
            except Exception as e:
                if "duplicate column name" in str(e).lower():
                    running_logger.debug(f"字段 {field_name} 已存在，跳过")
                else:
                    running_logger.error(f"添加字段 {field_name} 失败: {e}")

        session.commit()
        if success_count > 0:
            running_logger.info(f"表 {table_name} 结构更新完成，成功添加了 {success_count} 个字段")
        return True

    except Exception as e:
        session.rollback()
        running_logger.error(f"更新表 {table_name} 结构失败: {e}")
        return False
    finally:
        session.close()

# 7. 从字典创建模型实例的函数 (修改为支持自定义 data_dict)
def create_instance_from_dict(model_class, device, custom_data_dict=None):
    """
    从字典创建 SQLAlchemy 模型实例
    :param model_class: SQLAlchemy 模型类
    :param device: 设备实例
    :param custom_data_dict: 可选，自定义数据字典（用于 cluster）
    :return: 模型实例
    """
    mapper = inspect(model_class)
    valid_fields = {col.key for col in mapper.columns}

    used_data_dict = custom_data_dict if custom_data_dict else device.data_dict

    instance_data = {
        "timestamp": datetime.datetime.now(),
        "online_status": device.online_status,
    }

    if instance_data["timestamp"] is None:
        now = datetime.datetime.now()
        instance_data["timestamp"] = datetime.datetime.strptime(
            now.strftime('%Y-%m-%d %H:%M:%S'), '%Y-%m-%d %H:%M:%S'
        )
    elif isinstance(instance_data["timestamp"], str):
        try:
            instance_data["timestamp"] = datetime.datetime.strptime(
                instance_data["timestamp"], '%Y-%m-%d %H:%M:%S'
            )
        except ValueError:
            raise ValueError("Invalid timestamp format, expected '%Y-%m-%d %H:%M:%S'")

    instance_data.update(
        {k: v['value'] for k, v in used_data_dict.items() if k in valid_fields and k not in ["timestamp", "online_status"]}
    )

    for col in mapper.columns:
        if not col.nullable and col.key not in instance_data and col.key != "id":
            raise ValueError(f"Missing required field: {col.key}")

    instance = model_class(**instance_data)
    return instance

# 8. 示例：操作数据库
def insert_into_db(device_list):
    """批量插入设备数据"""
    Session = sessionmaker(bind=engine)
    session = Session()

    try:
        instances = []
        for device in device_list:
            table_name = device.name.lower()
            if device.online_status==0:
                running_logger.warning(
                    f"设备 {device.name} 不在线，跳过插入")
                continue
            # 检查 data_dict 是否有效
            if not hasattr(device, 'data_dict') or not device.data_dict:
                running_logger.warning(f"设备 {device.name} (ID: {getattr(device, 'id', '无')}) data_dict 为空，跳过插入")
                continue
            # 检查模型是否存在
            if table_name not in global_models:
                running_logger.info(f"模型 {table_name} 不存在，尝试创建")
                global_models[table_name] = create_model_class(device)
                Base.metadata.create_all(engine)
            model = global_models[table_name]
            instance = create_instance_from_dict(model, device)
            instances.append(instance)

            # 针对 BMS (GtBms) 的特殊处理，插入 cluster 数据
            if device.name.lower() == "gt_bms_tcp":  # 或使用 isinstance(device, GtBms) 如果已导入 GtBms
                # cluster_data = device.list_for_data_cluster_dict
                for i, cluster_data in enumerate(device.list_for_data_cluster_dict, start=1):
                    cluster_table_name = f"cluster{i}"
                    if cluster_table_name not in global_models:
                        running_logger.info(f"模型 {cluster_table_name} 不存在，尝试创建")
                        global_models[cluster_table_name] = create_model_class(
                            data_dict=cluster_data,
                            custom_table_name=cluster_table_name,
                            custom_class_name=f"Cluster{i}"
                        )
                        Base.metadata.create_all(engine)
                    cluster_model = global_models[cluster_table_name]
                    cluster_instance = create_instance_from_dict(cluster_model, device, custom_data_dict=cluster_data)
                    instances.append(cluster_instance)

        if instances:
            session.add_all(instances)
            session.commit()
            running_logger.info(f'插入 {len(instances)} 条数据成功，时间: {datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")}')
        else:
            running_logger.warning("无有效数据插入")
    except Exception as e:
        session.rollback()
        running_logger.error(f'插入数据库失败: {e}')
    finally:
        session.expunge_all()
        session.close()
        gc.collect()


def bulk_insert_into_db(device_list):
    """使用 bulk_insert_mappings 批量插入设备数据（更高性能）"""
    Session = sessionmaker(bind=engine)
    session = Session()
    try:
        # 按表名分组数据
        table_data = defaultdict(list)

        for device in device_list:
            if device.online_status==0:
                running_logger.warning(
                    f"设备 {device.name} 不在线，跳过插入")
                continue
            # 检查 data_dict 是否有效
            if not hasattr(device, 'data_dict') or not device.data_dict:
                running_logger.warning(
                    f"设备 {device.name} (ID: {getattr(device, 'id', '无')}) data_dict 为空，跳过插入")
                continue

            table_name = device.name.lower()

            # 确保模型存在
            if table_name not in global_models:
                running_logger.info(f"模型 {table_name} 不存在，尝试创建")
                global_models[table_name] = create_model_class(device)
                Base.metadata.create_all(engine)

            model = global_models[table_name]
            mapper = inspect(model)
            valid_fields = {col.key for col in mapper.columns}

            # 准备基础数据
            base_data = {
                "timestamp": datetime.datetime.now(),
                "online_status": device.online_status,
            }

            # 处理时间戳格式
            if base_data["timestamp"] is None:
                base_data["timestamp"] = datetime.datetime.now()
            elif isinstance(base_data["timestamp"], str):
                try:
                    base_data["timestamp"] = datetime.datetime.strptime(
                        base_data["timestamp"], '%Y-%m-%d %H:%M:%S'
                    )
                except ValueError:
                    base_data["timestamp"] = datetime.datetime.now()

            # 添加设备数据字段
            data_row = base_data.copy()
            data_row.update(
                {k: v['value'] for k, v in device.data_dict.items()
                 if k in valid_fields and k not in ["timestamp", "online_status"]}
            )

            table_data[table_name].append(data_row)

            # 处理 BMS 的 cluster 数据
            if device.name.lower() == "gt_bms_tcp":
                for i, cluster_data in enumerate(device.list_for_data_cluster_dict, start=1):
                    cluster_table_name = f"cluster{i}"

                    if cluster_table_name not in global_models:
                        running_logger.info(f"模型 {cluster_table_name} 不存在，尝试创建")
                        global_models[cluster_table_name] = create_model_class(
                            data_dict=cluster_data,
                            custom_table_name=cluster_table_name,
                            custom_class_name=f"Cluster{i}"
                        )
                        Base.metadata.create_all(engine)

                    cluster_model = global_models[cluster_table_name]
                    cluster_mapper = inspect(cluster_model)
                    cluster_valid_fields = {col.key for col in cluster_mapper.columns}

                    # 准备 cluster 数据
                    cluster_base_data = {
                        "timestamp": base_data["timestamp"],
                        "online_status": device.online_status,
                    }

                    cluster_data_row = cluster_base_data.copy()
                    cluster_data_row.update(
                        {k: v['value'] for k, v in cluster_data.items()
                         if k in cluster_valid_fields and k not in ["timestamp", "online_status"]}
                    )

                    table_data[cluster_table_name].append(cluster_data_row)

        # 批量插入所有表的数据
        total_records = 0
        for table_name, records in table_data.items():
            if records:
                model = global_models[table_name]
                session.bulk_insert_mappings(model, records)
                total_records += len(records)
                running_logger.info(f"表 {table_name} 批量插入 {len(records)} 条记录")

        session.commit()
        running_logger.info(
            f'批量插入 {total_records} 条数据成功，时间: {datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")}')

    except Exception as e:
        session.rollback()
        running_logger.error(f'批量插入数据库失败: {e}')
    finally:
        session.close()
        gc.collect()


def bulk_insert_into_db_from_redis(redis_conn, dev_name_list, engine, dev_dict):
    """从 Redis 获取数据并批量插入数据库，包含表结构检查"""
    Session = sessionmaker(bind=engine)
    session = Session()

    try:
        # 按表名分组数据
        table_data = defaultdict(list)

        skipped_missing = []
        skipped_offline = []
        skipped_empty = []
        for name in dev_name_list:
            key = f"device:{name}"
            serialized = redis_conn.get(key)
            if not serialized:
                skipped_missing.append(name)
                continue

            data = json.loads(serialized)
            data_dict = data.get('data', {})
            timestamp_str = data.get('timestamp')
            online_status = data.get('online_status', 0)

            if online_status == 0:
                skipped_offline.append(name)
                continue

            if not data_dict:
                skipped_empty.append(name)
                continue

            table_name = name.lower()

            # 确保模型存在
            if table_name not in global_models:
                running_logger.info(f"模型 {table_name} 不存在，尝试创建")
                dev = dev_dict.get(name)
                if dev:
                    global_models[table_name] = create_model_class(dev)
                    Base.metadata.create_all(engine)
                    # 新创建的表，添加到缓存
                    _checked_tables.add(table_name)
                else:
                    running_logger.error(f"无法创建模型 for {name}，缺少设备实例")
                    continue

            # 使用缓存检查表结构（只在需要时检查）
            if table_name not in _checked_tables:
                if not check_and_update_table_structure_cached(engine, table_name, data_dict):
                    running_logger.error(f"表 {table_name} 结构检查失败，跳过插入")
                    continue
            # else:
            #     running_logger.debug(f"表 {table_name} 已缓存，跳过结构检查")

            model = global_models[table_name]
            mapper = inspect(model)
            valid_fields = {col.key for col in mapper.columns}

            # 解析时间戳
            # try:
            #     timestamp = datetime.datetime.fromisoformat(timestamp_str)
            # except (ValueError, TypeError):
            #     timestamp = datetime.datetime.now()

            try:
                timestamp = datetime.datetime.fromisoformat(timestamp_str)
            except (ValueError, TypeError):
                timestamp = datetime.datetime.now()

            # 核心修正：强制对齐到15秒间隔，并抹除微秒
            # 这样秒数只会出现：00, 15, 30, 45
            aligned_second = (timestamp.second // 15) * 15
            timestamp = timestamp.replace(second=aligned_second, microsecond=0)

            # 准备基础数据
            base_data = {
                "timestamp": timestamp,
                "online_status": online_status,
            }

            # 添加设备数据字段（只添加存在的字段）
            data_row = base_data.copy()
            for k, v in data_dict.items():
                if k in valid_fields and k not in ["timestamp", "online_status"]:
                    data_row[k] = v.get('value', 0)

            table_data[table_name].append(data_row)

            # 处理 BMS 的 cluster 数据
            if name.lower() == "gt_bms_tcp":
                cluster_data_list = data.get('cluster_data', [])
                for i, cluster_data in enumerate(cluster_data_list, start=1):
                    cluster_table_name = f"cluster{i}"

                    if cluster_table_name not in global_models:
                        running_logger.info(f"模型 {cluster_table_name} 不存在，尝试创建")
                        global_models[cluster_table_name] = create_model_class(
                            data_dict=cluster_data,
                            custom_table_name=cluster_table_name,
                            custom_class_name=f"Cluster{i}"
                        )
                        Base.metadata.create_all(engine)

                    # 检查cluster表结构
                    if not check_and_update_table_structure_cached(engine, cluster_table_name, cluster_data):
                        running_logger.error(f"cluster表 {cluster_table_name} 结构检查失败，跳过插入")
                        continue

                    cluster_model = global_models[cluster_table_name]
                    cluster_mapper = inspect(cluster_model)
                    cluster_valid_fields = {col.key for col in cluster_mapper.columns}

                    # 准备 cluster 数据
                    cluster_base_data = {
                        "timestamp": base_data["timestamp"],
                        "online_status": online_status,
                    }

                    cluster_data_row = cluster_base_data.copy()
                    for k, v in cluster_data.items():
                        if k in cluster_valid_fields and k not in ["timestamp", "online_status"]:
                            cluster_data_row[k] = v.get('value', 0)

                    table_data[cluster_table_name].append(cluster_data_row)

        # 批量插入所有表的数据
        total_records = 0
        success_tables = 0
        failed_tables = 0
        for table_name, records in table_data.items():
            if records:
                model = global_models[table_name]
                try:
                    session.bulk_insert_mappings(model, records)
                    total_records += len(records)
                    success_tables += 1
                except Exception as e:
                    running_logger.error(f"表 {table_name} 插入失败: {e}")
                    failed_tables += 1
                    # 继续处理其他表

        session.commit()
        running_logger.info(
            f"批量插入完成: 记录={total_records} 成功表={success_tables} 失败表={failed_tables} "
            f"跳过(无数据)={len(skipped_missing)} 跳过(离线)={len(skipped_offline)} 跳过(空字段)={len(skipped_empty)}"
        )

    except Exception as e:
        session.rollback()
        running_logger.error(f'批量插入数据库失败: {e}')
    finally:
        session.close()
        gc.collect()


def check_and_update_table_structure_cached(engine, table_name, expected_fields):
    """带缓存的表结构检查，避免重复检查"""

    # 如果表已经检查过且结构完整，直接返回
    if table_name in _checked_tables:
        return True

    inspector = inspect(engine)

    # 检查表是否存在，不存在则尝试创建
    if not inspector.has_table(table_name):
        running_logger.info(f"表 {table_name} 不存在，尝试创建")
        Base.metadata.create_all(engine)
        if not inspector.has_table(table_name):
            running_logger.error(f"表 {table_name} 创建失败")
            return False
        running_logger.info(f"表 {table_name} 创建成功")

    # 获取现有字段
    existing_columns = {col['name'] for col in inspector.get_columns(table_name)}
    expected_columns = set(expected_fields.keys())

    # 找出缺失的字段
    missing_columns = expected_columns - existing_columns

    if not missing_columns:
        _checked_tables.add(table_name)
        if table_name not in _cached_log_tables:
            running_logger.debug(f"表 {table_name} 结构完整，已缓存")
            _cached_log_tables.add(table_name)
        return True

    # 有缺失字段，需要更新
    result = check_and_update_table_structure_once(engine, table_name, expected_fields)

    # 如果更新成功，添加到缓存
    if result:
        _checked_tables.add(table_name)
        if table_name not in _cached_log_tables:
            running_logger.debug(f"表 {table_name} 字段更新成功，已缓存")
            _cached_log_tables.add(table_name)

    return result


def check_and_update_table_structure_once(engine, table_name, expected_fields):
    """一次性检查并更新表结构，避免重复检查"""
    inspector = inspect(engine)

    # 检查表是否存在
    if not inspector.has_table(table_name):
        running_logger.error(f"表 {table_name} 不存在")
        return False

    # 获取现有字段（使用精确匹配）
    existing_columns = {col['name'] for col in inspector.get_columns(table_name)}
    expected_columns = set(expected_fields.keys())

    # 使用精确匹配找出缺失的字段
    missing_columns = expected_columns - existing_columns

    if not missing_columns:
        # running_logger.debug(f"表 {table_name} 结构完整，无需更新")  # 改为debug级别，减少日志
        return True

    running_logger.info(f"表 {table_name} 缺失字段: {missing_columns}")

    # 添加缺失的字段
    Session = sessionmaker(bind=engine)
    session = Session()

    success_count = 0
    try:
        for field_name in missing_columns:
            # 再次检查字段是否存在（避免并发问题）
            current_columns = {col['name'] for col in inspector.get_columns(table_name)}
            if field_name in current_columns:
                running_logger.debug(f"字段 {field_name} 已存在，跳过添加")
                continue

            field_info = expected_fields[field_name]

            # 构建SQL类型
            if field_info["datatype"] in ["String", "STRING"]:
                length = field_info.get("length", 10)
                sql_type = f"VARCHAR({length})"
            elif field_info["datatype"] in ["Integer", "INT", "INT16", "UINT16", "INT32", "UINT32", "UINT"]:
                sql_type = "INTEGER"
            elif field_info["datatype"] in ["Float", "FLOAT", "DOUBLE"]:
                sql_type = "FLOAT"
            elif field_info["datatype"] in ["Boolean", "Bool", "bool", "BOOL"]:
                sql_type = "BOOLEAN"
            else:
                sql_type = "TEXT"

            # 转义字段名
            escaped_field_name = f'"{field_name}"'

            alter_sql = f'ALTER TABLE "{table_name}" ADD COLUMN {escaped_field_name} {sql_type}'

            try:
                session.execute(text(alter_sql))
                running_logger.info(f"成功为表 {table_name} 添加字段: {field_name}")
                success_count += 1
            except Exception as e:
                if "duplicate column name" in str(e).lower():
                    running_logger.debug(f"字段 {field_name} 已存在，跳过")
                else:
                    running_logger.error(f"添加字段 {field_name} 失败: {e}")

        session.commit()
        if success_count > 0:
            running_logger.info(f"表 {table_name} 结构更新完成，成功添加了 {success_count} 个字段")
        return True

    except Exception as e:
        session.rollback()
        running_logger.error(f"更新表 {table_name} 结构失败: {e}")
        return False
    finally:
        session.close()