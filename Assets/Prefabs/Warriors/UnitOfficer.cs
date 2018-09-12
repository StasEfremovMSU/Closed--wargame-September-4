using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class UnitOfficer : MonoBehaviour {

    /// <summary>
    /// Влияет на управление солдатами
    /// </summary>
    public float charisma;
    /// <summary>
    /// Влияет на скорость принятия решений
    /// </summary>
    public float tacticMind;
    /// <summary>
    /// Какого размера может взять под командование 
    /// </summary>
    public float LeadSkill;

    /// Типы командира:
    /// Офицер - 1
    /// Знаменосец - 3
    /// Музыкант - 4
    /// Адьютант - 0
    /// Плюс старшиекомнадные чины
    public int IdTypeOfficers;
    public int IdOfficer;


    /// <summary>
    /// Состояние 
    /// 1. Атаковать ближайшего
    /// </summary>
    public int statement;

    /// <summary>
    /// Дистанция, на которой может управлять
    /// </summary>
    public float LeadDist;
    public float moral;
    /// <summary>
    /// Сколько периодов провел с полком
    /// </summary>
    public int TurnsWithReg;
    /// <summary>
    /// Полк, в котором состоит 
    /// </summary>
    public int RegimentNum;
    /// <summary>
    ///Бонус к прохождению теста на панику управляемым полком
    /// </summary>
    public float BuffPanic;

    /// <summary>
    /// Возвращает компонент unit Soldiers
    /// </summary>
    /// <returns></returns>
    public UnitSoldiers GetSoldiers()
    {
        return gameObject.GetComponent<UnitSoldiers>();
    }

	// Use this for initialization
	void Start () {
		
	}
	
	// Update is called once per frame
	void Update () {
        if (IdTypeOfficers == 1)
        {
            
        }



        if (Input.GetKey(KeyCode.U))
            mainStatic.regiment_stat[gameObject.GetComponent<UnitSoldiers>().numReg].orderForw = true;

        if (Input.GetKey(KeyCode.J))
            mainStatic.regiment_stat[gameObject.GetComponent<UnitSoldiers>().numReg].orderBack = true;

        if (Input.GetKey(KeyCode.H))
            mainStatic.regiment_stat[gameObject.GetComponent<UnitSoldiers>().numReg].orderRotL = true;

        if (Input.GetKey(KeyCode.K))
            mainStatic.regiment_stat[gameObject.GetComponent<UnitSoldiers>().numReg].orderRotW = true;

        if (Input.GetKey(KeyCode.T))
        {
            mainStatic.regiment_stat[gameObject.GetComponent<UnitSoldiers>().numReg].archTotewer = true;
            mainStatic.regiment_stat[gameObject.GetComponent<UnitSoldiers>().numReg].archAlone = false;
        }

        if (Input.GetKey(KeyCode.G))
        {
            mainStatic.regiment_stat[gameObject.GetComponent<UnitSoldiers>().numReg].archAlone = true;
            mainStatic.regiment_stat[gameObject.GetComponent<UnitSoldiers>().numReg].archTotewer = false;
        }

	}
}
